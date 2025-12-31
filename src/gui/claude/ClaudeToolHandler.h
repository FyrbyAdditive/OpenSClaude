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

class MainWindow;
class EditorInterface;

namespace Claude {

struct ToolResult {
  bool success;
  QString content;
  bool isError{false};
};

class ToolHandler : public QObject
{
  Q_OBJECT

public:
  explicit ToolHandler(MainWindow *mainWindow, QObject *parent = nullptr);

  // Get tool definitions for Anthropic API
  QJsonArray getToolDefinitions() const;

  // Execute a tool and return the result
  ToolResult executeTool(const QString& toolName, const QJsonObject& input);

private:
  // Tool implementations
  ToolResult readEditor();
  ToolResult writeEditor(const QString& content);
  ToolResult getSelection();
  ToolResult replaceSelection(const QString& content);
  ToolResult insertAtCursor(const QString& content);
  ToolResult runPreview();
  ToolResult runRender();
  ToolResult getConsole(int maxLines);
  ToolResult getErrors();
  ToolResult getFilePath();

  EditorInterface* activeEditor() const;

  MainWindow *mainWindow_;
};

} // namespace Claude
