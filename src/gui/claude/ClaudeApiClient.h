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

#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>

namespace Claude {

class ApiClient : public QObject
{
  Q_OBJECT

public:
  explicit ApiClient(QObject *parent = nullptr);
  ~ApiClient() override;

  // Configuration
  void setApiKey(const QString& apiKey);
  QString apiKey() const { return apiKey_; }
  bool isConfigured() const { return !apiKey_.isEmpty(); }

  // Message sending (streaming)
  void sendMessage(const QString& modelId,
                   const QJsonArray& messages,
                   const QJsonArray& tools,
                   const QString& systemPrompt = QString(),
                   int maxTokens = 4096);

  void cancelRequest();
  bool isRequestInProgress() const { return currentReply_ != nullptr; }

signals:
  void streamStarted();
  void contentDelta(const QString& text);
  void toolUseStarted(const QString& toolId, const QString& toolName);
  void toolUseInputDelta(const QString& toolId, const QString& inputJsonChunk);
  void toolUseComplete(const QString& toolId, const QString& toolName, const QJsonObject& input);
  void messageComplete(const QJsonObject& fullMessage);
  void errorOccurred(const QString& error);

private slots:
  void onReadyRead();
  void onFinished();
  void onError(QNetworkReply::NetworkError error);

private:
  void parseSSEChunk(const QByteArray& chunk);
  void processSSEEvent(const QString& eventType, const QString& data);
  void handleContentBlockStart(const QJsonObject& data);
  void handleContentBlockDelta(const QJsonObject& data);
  void handleContentBlockStop(const QJsonObject& data);
  void handleMessageDelta(const QJsonObject& data);

  QNetworkAccessManager *nam_;
  QNetworkReply *currentReply_{nullptr};
  QString apiKey_;

  // SSE parsing state
  QByteArray sseBuffer_;

  // Current message state
  QJsonObject currentMessage_;
  QJsonArray contentBlocks_;
  int currentBlockIndex_{-1};

  // Current tool use accumulation
  QString currentToolId_;
  QString currentToolName_;
  QString currentToolInputJson_;

  // Current text accumulation
  QString currentTextContent_;

  static constexpr const char* API_URL = "https://api.anthropic.com/v1/messages";
  static constexpr const char* API_VERSION = "2023-06-01";
};

} // namespace Claude
