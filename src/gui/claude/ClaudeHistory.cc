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

#include "ClaudeHistory.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace Claude {

History::History(QObject *parent)
  : QObject(parent)
{
}

void History::setFilePath(const QString& scadFilePath)
{
  if (scadFilePath_ == scadFilePath) {
    return;
  }

  // Save current history before switching
  if (!scadFilePath_.isEmpty() && !messages_.isEmpty()) {
    save();
  }

  scadFilePath_ = scadFilePath;
  messages_.clear();

  // Load history for new file
  if (!scadFilePath_.isEmpty()) {
    load();
  }

  emit historyChanged();
}

QString History::historyFilePath() const
{
  return computeHistoryPath(scadFilePath_);
}

QString History::computeHistoryPath(const QString& scadPath)
{
  if (scadPath.isEmpty()) {
    return QString();
  }
  return scadPath + ".claude-history.json";
}

void History::addMessage(const Message& msg)
{
  messages_.append(msg);
  emit historyChanged();
}

void History::clear()
{
  messages_.clear();

  // Delete history file
  QString historyPath = historyFilePath();
  if (!historyPath.isEmpty()) {
    QFile::remove(historyPath);
  }

  emit historyChanged();
}

QJsonArray History::toApiMessages() const
{
  QJsonArray apiMessages;

  // Group consecutive tool_use/tool_result pairs correctly
  int i = 0;
  while (i < messages_.size()) {
    const Message& msg = messages_[i];

    if (msg.role == Message::Role::ToolUse) {
      // Tool use from assistant - need to bundle with any text before it
      QJsonObject assistantMsg;
      assistantMsg["role"] = "assistant";

      QJsonArray contentArray;

      // Add the tool use
      QJsonObject toolUse;
      toolUse["type"] = "tool_use";
      toolUse["id"] = msg.toolId;
      toolUse["name"] = msg.toolName;
      toolUse["input"] = msg.toolInput;
      contentArray.append(toolUse);

      assistantMsg["content"] = contentArray;
      apiMessages.append(assistantMsg);

      ++i;

      // Look for corresponding tool result
      if (i < messages_.size() && messages_[i].role == Message::Role::ToolResult) {
        const Message& resultMsg = messages_[i];

        QJsonObject userMsg;
        userMsg["role"] = "user";

        QJsonArray resultArray;
        QJsonObject toolResult;
        toolResult["type"] = "tool_result";
        toolResult["tool_use_id"] = resultMsg.toolId;
        toolResult["content"] = resultMsg.content;
        if (resultMsg.isError) {
          toolResult["is_error"] = true;
        }
        resultArray.append(toolResult);

        userMsg["content"] = resultArray;
        apiMessages.append(userMsg);

        ++i;
      }
    } else {
      // Regular user or assistant message
      apiMessages.append(msg.toApiFormat());
      ++i;
    }
  }

  return apiMessages;
}

void History::save()
{
  QString historyPath = historyFilePath();
  if (historyPath.isEmpty()) {
    return;
  }

  QJsonObject root;
  root["version"] = HISTORY_VERSION;
  root["source_file"] = QFileInfo(scadFilePath_).fileName();

  QJsonArray messagesArray;
  for (const Message& msg : messages_) {
    messagesArray.append(msg.toHistoryFormat());
  }
  root["messages"] = messagesArray;

  QFile file(historyPath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
  }
}

void History::load()
{
  QString historyPath = historyFilePath();
  if (historyPath.isEmpty()) {
    return;
  }

  QFile file(historyPath);
  if (!file.exists()) {
    return;
  }

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject()) {
    return;
  }

  QJsonObject root = doc.object();
  int version = root["version"].toInt();
  if (version != HISTORY_VERSION) {
    // Future: handle version migrations
    return;
  }

  messages_.clear();
  QJsonArray messagesArray = root["messages"].toArray();
  for (const QJsonValue& value : messagesArray) {
    messages_.append(Message::fromHistoryFormat(value.toObject()));
  }
}

} // namespace Claude
