/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2024 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "ClaudeApiClient.h"

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrl>

#include "platform/PlatformUtils.h"

namespace Claude {

ApiClient::ApiClient(QObject *parent)
  : QObject(parent)
  , nam_(new QNetworkAccessManager(this))
  , retryTimer_(new QTimer(this))
{
  retryTimer_->setSingleShot(true);
  connect(retryTimer_, &QTimer::timeout, this, &ApiClient::onRetryTimer);
}

ApiClient::~ApiClient()
{
  cancelRequest();
}

void ApiClient::setApiKey(const QString& apiKey)
{
  apiKey_ = apiKey;
}

void ApiClient::sendMessage(const QString& modelId,
                            const QJsonArray& messages,
                            const QJsonArray& tools,
                            const QString& systemPrompt,
                            int maxTokens)
{
  if (!isConfigured()) {
    emit errorOccurred("API key not configured");
    return;
  }

  if (isRequestInProgress()) {
    emit errorOccurred("Request already in progress");
    return;
  }

  // Store parameters for potential retry
  pendingModelId_ = modelId;
  pendingMessages_ = messages;
  pendingTools_ = tools;
  pendingSystemPrompt_ = systemPrompt;
  pendingMaxTokens_ = maxTokens;

  // Reset state
  sseBuffer_.clear();
  currentMessage_ = QJsonObject();
  contentBlocks_ = QJsonArray();
  currentBlockIndex_ = -1;
  currentToolId_.clear();
  currentToolName_.clear();
  currentToolInputJson_.clear();
  currentTextContent_.clear();

  // Build request body
  QJsonObject body;
  body["model"] = modelId;
  body["max_tokens"] = maxTokens;
  body["stream"] = true;
  body["messages"] = messages;

  // Use structured system prompt with cache_control for prompt caching
  if (!systemPrompt.isEmpty()) {
    QJsonArray systemArray;
    QJsonObject systemBlock;
    systemBlock["type"] = "text";
    systemBlock["text"] = systemPrompt;
    QJsonObject cacheControl;
    cacheControl["type"] = "ephemeral";
    systemBlock["cache_control"] = cacheControl;
    systemArray.append(systemBlock);
    body["system"] = systemArray;
  }

  // Add tools with cache_control on the last tool for prompt caching
  if (!tools.isEmpty()) {
    QJsonArray toolsCopy = tools;
    QJsonObject lastTool = toolsCopy.last().toObject();
    QJsonObject cacheControl;
    cacheControl["type"] = "ephemeral";
    lastTool["cache_control"] = cacheControl;
    toolsCopy[toolsCopy.size() - 1] = lastTool;
    body["tools"] = toolsCopy;
  }

  // Create request
  QUrl apiUrl(API_URL);
  QNetworkRequest request(apiUrl);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader("x-api-key", apiKey_.toUtf8());
  request.setRawHeader("anthropic-version", API_VERSION);
  request.setRawHeader("anthropic-beta", "prompt-caching-2024-07-31");
  request.setRawHeader("User-Agent", QString::fromStdString(PlatformUtils::user_agent()).toUtf8());

  // Send POST request
  QByteArray requestData = QJsonDocument(body).toJson(QJsonDocument::Compact);
  currentReply_ = nam_->post(request, requestData);

  // Connect signals
  connect(currentReply_, &QNetworkReply::readyRead, this, &ApiClient::onReadyRead);
  connect(currentReply_, &QNetworkReply::finished, this, &ApiClient::onFinished);
  connect(currentReply_, &QNetworkReply::errorOccurred, this, &ApiClient::onError);

  emit streamStarted();
}

void ApiClient::cancelRequest()
{
  // Stop any pending retry
  if (retryTimer_) {
    retryTimer_->stop();
  }
  retryCount_ = 0;

  // QPointer automatically becomes null if the object was already deleted
  // Take the pointer atomically and clear immediately
  QNetworkReply *reply = currentReply_.data();
  currentReply_.clear();

  if (reply) {
    // Block all signals from this reply to prevent any callbacks during cleanup
    reply->blockSignals(true);

    // Abort the request
    reply->abort();

    // Schedule for deletion - safe now that signals are blocked
    reply->deleteLater();
  }
}

void ApiClient::onReadyRead()
{
  if (!currentReply_) return;

  QByteArray data = currentReply_->readAll();
  sseBuffer_.append(data);

  // Parse complete SSE events
  while (true) {
    int eventEnd = sseBuffer_.indexOf("\n\n");
    if (eventEnd == -1) break;

    QByteArray eventData = sseBuffer_.left(eventEnd);
    sseBuffer_.remove(0, eventEnd + 2);

    parseSSEChunk(eventData);
  }
}

void ApiClient::onFinished()
{
  if (!currentReply_) return;

  // Process any remaining buffer
  if (!sseBuffer_.isEmpty()) {
    parseSSEChunk(sseBuffer_);
    sseBuffer_.clear();
  }

  // Reset retry count on successful completion
  retryCount_ = 0;

  // Build final message
  currentMessage_["content"] = contentBlocks_;

  // Clean up reply BEFORE emitting signal, so slot can start a new request
  currentReply_->deleteLater();
  currentReply_.clear();

  emit messageComplete(currentMessage_);
}

void ApiClient::onError(QNetworkReply::NetworkError error)
{
  if (!currentReply_) return;

  if (error == QNetworkReply::OperationCanceledError) {
    // User cancelled, don't emit error
    return;
  }

  int httpStatus = currentReply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QString errorMsg;

  // Try to extract error message from response body
  QByteArray responseBody = currentReply_->readAll();
  if (!responseBody.isEmpty()) {
    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.isObject()) {
      QJsonObject obj = doc.object();
      if (obj.contains("error")) {
        QJsonObject errorObj = obj["error"].toObject();
        QString apiError = errorObj["message"].toString();
        QString errorType = errorObj["type"].toString();
        if (!apiError.isEmpty()) {
          errorMsg = QString("%1: %2").arg(errorType, apiError);
        }
      }
    }
  }

  // Handle rate limiting with automatic retry
  if (httpStatus == 429 && retryCount_ < MAX_RETRIES) {
    // Get retry-after header (in seconds)
    int retryAfterSecs = DEFAULT_RETRY_DELAY_MS / 1000;
    QByteArray retryAfterHeader = currentReply_->rawHeader("retry-after");
    if (!retryAfterHeader.isEmpty()) {
      bool ok;
      int headerValue = retryAfterHeader.toInt(&ok);
      if (ok && headerValue > 0) {
        retryAfterSecs = headerValue;
      }
    }

    // Clean up current reply
    currentReply_->deleteLater();
    currentReply_.clear();

    // Schedule retry
    retryCount_++;
    emit rateLimitWaiting(retryAfterSecs);
    retryTimer_->start(retryAfterSecs * 1000);
    return;
  }

  // If we couldn't extract a specific error, build a descriptive one
  if (errorMsg.isEmpty()) {
    if (httpStatus > 0) {
      switch (httpStatus) {
        case 400: errorMsg = "Bad request - check your message format"; break;
        case 401: errorMsg = "Invalid API key - please check your API key in settings"; break;
        case 403: errorMsg = "Access forbidden - your API key may not have permission"; break;
        case 404: errorMsg = "API endpoint not found"; break;
        case 429: errorMsg = "Rate limited - too many requests. Max retries exceeded."; break;
        case 500: errorMsg = "Anthropic server error - try again later"; break;
        case 529: errorMsg = "Anthropic API overloaded - try again later"; break;
        default: errorMsg = QString("HTTP error %1").arg(httpStatus); break;
      }
    } else {
      errorMsg = currentReply_->errorString();
      if (errorMsg.contains("server replied:")) {
        errorMsg = "Network error - check your internet connection";
      }
    }
  }

  // Reset retry count on non-rate-limit errors
  retryCount_ = 0;

  emit errorOccurred(errorMsg);

  currentReply_->deleteLater();
  currentReply_.clear();
}

void ApiClient::onRetryTimer()
{
  // Retry the pending request
  // Reset state for new attempt
  sseBuffer_.clear();
  currentMessage_ = QJsonObject();
  contentBlocks_ = QJsonArray();
  currentBlockIndex_ = -1;
  currentToolId_.clear();
  currentToolName_.clear();
  currentToolInputJson_.clear();
  currentTextContent_.clear();

  // Build request body
  QJsonObject body;
  body["model"] = pendingModelId_;
  body["max_tokens"] = pendingMaxTokens_;
  body["stream"] = true;
  body["messages"] = pendingMessages_;

  // Use structured system prompt with cache_control for prompt caching
  if (!pendingSystemPrompt_.isEmpty()) {
    QJsonArray systemArray;
    QJsonObject systemBlock;
    systemBlock["type"] = "text";
    systemBlock["text"] = pendingSystemPrompt_;
    QJsonObject cacheControl;
    cacheControl["type"] = "ephemeral";
    systemBlock["cache_control"] = cacheControl;
    systemArray.append(systemBlock);
    body["system"] = systemArray;
  }

  // Add tools with cache_control on the last tool for prompt caching
  if (!pendingTools_.isEmpty()) {
    QJsonArray toolsCopy = pendingTools_;
    QJsonObject lastTool = toolsCopy.last().toObject();
    QJsonObject cacheControl;
    cacheControl["type"] = "ephemeral";
    lastTool["cache_control"] = cacheControl;
    toolsCopy[toolsCopy.size() - 1] = lastTool;
    body["tools"] = toolsCopy;
  }

  // Create request
  QUrl apiUrl(API_URL);
  QNetworkRequest request(apiUrl);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader("x-api-key", apiKey_.toUtf8());
  request.setRawHeader("anthropic-version", API_VERSION);
  request.setRawHeader("anthropic-beta", "prompt-caching-2024-07-31");
  request.setRawHeader("User-Agent", QString::fromStdString(PlatformUtils::user_agent()).toUtf8());

  // Send POST request
  QByteArray requestData = QJsonDocument(body).toJson(QJsonDocument::Compact);
  currentReply_ = nam_->post(request, requestData);

  // Connect signals
  connect(currentReply_, &QNetworkReply::readyRead, this, &ApiClient::onReadyRead);
  connect(currentReply_, &QNetworkReply::finished, this, &ApiClient::onFinished);
  connect(currentReply_, &QNetworkReply::errorOccurred, this, &ApiClient::onError);

  emit streamStarted();
}

void ApiClient::parseSSEChunk(const QByteArray& chunk)
{
  // SSE format: event: <type>\ndata: <json>\n\n
  QString chunkStr = QString::fromUtf8(chunk);
  QStringList lines = chunkStr.split('\n');

  QString eventType;
  QString data;

  for (const QString& line : lines) {
    if (line.startsWith("event: ")) {
      eventType = line.mid(7).trimmed();
    } else if (line.startsWith("data: ")) {
      data = line.mid(6);
    }
  }

  if (!eventType.isEmpty() && !data.isEmpty()) {
    processSSEEvent(eventType, data);
  }
}

void ApiClient::processSSEEvent(const QString& eventType, const QString& data)
{
  QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
  if (!doc.isObject()) return;

  QJsonObject obj = doc.object();

  if (eventType == "message_start") {
    currentMessage_ = obj["message"].toObject();
  } else if (eventType == "content_block_start") {
    handleContentBlockStart(obj);
  } else if (eventType == "content_block_delta") {
    handleContentBlockDelta(obj);
  } else if (eventType == "content_block_stop") {
    handleContentBlockStop(obj);
  } else if (eventType == "message_delta") {
    handleMessageDelta(obj);
  } else if (eventType == "message_stop") {
    // Message complete, handled in onFinished
  } else if (eventType == "error") {
    QJsonObject errorObj = obj["error"].toObject();
    emit errorOccurred(errorObj["message"].toString());
  }
}

void ApiClient::handleContentBlockStart(const QJsonObject& data)
{
  currentBlockIndex_ = data["index"].toInt();
  QJsonObject contentBlock = data["content_block"].toObject();
  QString blockType = contentBlock["type"].toString();

  if (blockType == "text") {
    currentTextContent_.clear();
  } else if (blockType == "tool_use") {
    currentToolId_ = contentBlock["id"].toString();
    currentToolName_ = contentBlock["name"].toString();
    currentToolInputJson_.clear();
    qDebug() << "Claude: Tool use started -" << currentToolName_ << "id:" << currentToolId_;
    emit toolUseStarted(currentToolId_, currentToolName_);
  }
}

void ApiClient::handleContentBlockDelta(const QJsonObject& data)
{
  QJsonObject delta = data["delta"].toObject();
  QString deltaType = delta["type"].toString();

  if (deltaType == "text_delta") {
    QString text = delta["text"].toString();
    currentTextContent_.append(text);
    emit contentDelta(text);
  } else if (deltaType == "input_json_delta") {
    QString partialJson = delta["partial_json"].toString();
    currentToolInputJson_.append(partialJson);
    emit toolUseInputDelta(currentToolId_, partialJson);
  }
}

void ApiClient::handleContentBlockStop(const QJsonObject& data)
{
  Q_UNUSED(data);

  // Finalize the current content block
  if (!currentToolId_.isEmpty()) {
    // Parse the accumulated tool input JSON
    QJsonParseError parseError;
    QJsonDocument inputDoc = QJsonDocument::fromJson(currentToolInputJson_.toUtf8(), &parseError);
    QJsonObject toolInput;

    if (parseError.error != QJsonParseError::NoError) {
      qWarning() << "Claude: Failed to parse tool input JSON:" << parseError.errorString();
      qWarning() << "Claude: Raw JSON was:" << currentToolInputJson_.left(500);
    } else {
      toolInput = inputDoc.object();
    }

    // Add to content blocks
    QJsonObject toolUseBlock;
    toolUseBlock["type"] = "tool_use";
    toolUseBlock["id"] = currentToolId_;
    toolUseBlock["name"] = currentToolName_;
    toolUseBlock["input"] = toolInput;
    contentBlocks_.append(toolUseBlock);

    qDebug() << "Claude: Tool use complete -" << currentToolName_ << "input keys:" << toolInput.keys();
    emit toolUseComplete(currentToolId_, currentToolName_, toolInput);

    currentToolId_.clear();
    currentToolName_.clear();
    currentToolInputJson_.clear();
  } else if (!currentTextContent_.isEmpty()) {
    // Add text block
    QJsonObject textBlock;
    textBlock["type"] = "text";
    textBlock["text"] = currentTextContent_;
    contentBlocks_.append(textBlock);

    currentTextContent_.clear();
  }
}

void ApiClient::handleMessageDelta(const QJsonObject& data)
{
  QJsonObject delta = data["delta"].toObject();

  // Update message metadata
  if (delta.contains("stop_reason")) {
    currentMessage_["stop_reason"] = delta["stop_reason"];
  }
  if (delta.contains("stop_sequence")) {
    currentMessage_["stop_sequence"] = delta["stop_sequence"];
  }

  // Update usage
  if (data.contains("usage")) {
    currentMessage_["usage"] = data["usage"];
  }
}

} // namespace Claude
