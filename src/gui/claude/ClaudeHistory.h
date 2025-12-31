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
#include <QVector>
#include <QJsonArray>

#include "ClaudeMessage.h"

namespace Claude {

class History : public QObject
{
  Q_OBJECT

public:
  explicit History(QObject *parent = nullptr);

  // File association
  void setFilePath(const QString& scadFilePath);
  QString filePath() const { return scadFilePath_; }
  QString historyFilePath() const;

  // Message management
  void addMessage(const Message& msg);
  void clear();
  const QVector<Message>& messages() const { return messages_; }
  int messageCount() const { return messages_.size(); }

  // Convert to Anthropic API format
  QJsonArray toApiMessages() const;

  // Persistence
  void save();
  void load();

signals:
  void historyChanged();

private:
  static QString computeHistoryPath(const QString& scadPath);

  QString scadFilePath_;
  QVector<Message> messages_;

  static constexpr int HISTORY_VERSION = 1;
};

} // namespace Claude
