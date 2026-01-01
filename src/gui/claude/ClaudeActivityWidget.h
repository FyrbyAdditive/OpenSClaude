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

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QMap>
#include <QTimer>

namespace Claude {

class ActivityWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ActivityWidget(QWidget *parent = nullptr);

  // Clear all activity
  void clear();

  // Add a tool that's starting (shows spinner)
  void addTool(const QString& toolName);

  // Mark a tool as complete
  void completeTool(const QString& toolName, bool success);

  // Get list of tools that were used (for summary)
  QStringList executedTools() const { return executedTools_; }

  // Human-readable tool descriptions
  static QString humanReadableName(const QString& toolName);
  static QString humanReadableStatus(const QString& toolName);

private:
  void updateLayout();

  QVBoxLayout *layout_;
  QMap<QString, QLabel*> toolLabels_;
  QStringList executedTools_;
  QLabel *currentToolLabel_{nullptr};
  QTimer *animationTimer_;
  int animationFrame_{0};
};

} // namespace Claude
