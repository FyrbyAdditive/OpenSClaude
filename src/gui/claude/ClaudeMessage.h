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

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>

namespace Claude {

// Represents a single message in the conversation
struct Message {
  enum class Role {
    User,
    Assistant,
    ToolUse,
    ToolResult
  };

  Role role;
  QString content;
  QDateTime timestamp;
  QString model;           // Which model generated this (for assistant messages)
  QString toolId;          // For tool_use and tool_result
  QString toolName;        // For tool_use
  QJsonObject toolInput;   // For tool_use
  bool isError{false};     // For tool_result

  Message() : role(Role::User), timestamp(QDateTime::currentDateTime()) {}

  Message(Role r, const QString& c)
    : role(r), content(c), timestamp(QDateTime::currentDateTime()) {}

  // Convert to Anthropic API message format
  QJsonObject toApiFormat() const {
    QJsonObject msg;

    switch (role) {
      case Role::User:
        msg["role"] = "user";
        msg["content"] = content;
        break;
      case Role::Assistant:
        msg["role"] = "assistant";
        msg["content"] = content;
        break;
      case Role::ToolUse:
        msg["role"] = "assistant";
        {
          QJsonArray contentArray;
          QJsonObject toolUse;
          toolUse["type"] = "tool_use";
          toolUse["id"] = toolId;
          toolUse["name"] = toolName;
          toolUse["input"] = toolInput;
          contentArray.append(toolUse);
          msg["content"] = contentArray;
        }
        break;
      case Role::ToolResult:
        msg["role"] = "user";
        {
          QJsonArray contentArray;
          QJsonObject toolResult;
          toolResult["type"] = "tool_result";
          toolResult["tool_use_id"] = toolId;
          toolResult["content"] = content;
          if (isError) {
            toolResult["is_error"] = true;
          }
          contentArray.append(toolResult);
          msg["content"] = contentArray;
        }
        break;
    }

    return msg;
  }

  // Convert to JSON for history storage
  QJsonObject toHistoryFormat() const {
    QJsonObject obj;
    obj["role"] = static_cast<int>(role);
    obj["content"] = content;
    obj["timestamp"] = timestamp.toString(Qt::ISODate);
    if (!model.isEmpty()) {
      obj["model"] = model;
    }
    if (!toolId.isEmpty()) {
      obj["tool_id"] = toolId;
    }
    if (!toolName.isEmpty()) {
      obj["tool_name"] = toolName;
    }
    if (!toolInput.isEmpty()) {
      obj["tool_input"] = toolInput;
    }
    if (isError) {
      obj["is_error"] = true;
    }
    return obj;
  }

  // Load from history JSON
  static Message fromHistoryFormat(const QJsonObject& obj) {
    Message msg;
    msg.role = static_cast<Role>(obj["role"].toInt());
    msg.content = obj["content"].toString();
    msg.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
    msg.model = obj["model"].toString();
    msg.toolId = obj["tool_id"].toString();
    msg.toolName = obj["tool_name"].toString();
    msg.toolInput = obj["tool_input"].toObject();
    msg.isError = obj["is_error"].toBool();
    return msg;
  }
};

// Available Claude models
struct ModelInfo {
  QString id;
  QString displayName;
  int contextWindow;
  int maxOutputTokens;
};

inline QVector<ModelInfo> availableModels() {
  return {
    {"claude-sonnet-4-20250514", "Claude Sonnet 4", 200000, 16000},
    {"claude-opus-4-20250514", "Claude Opus 4", 200000, 32000},
    {"claude-haiku-3-5-20241022", "Claude 3.5 Haiku", 200000, 8192}
  };
}

} // namespace Claude
