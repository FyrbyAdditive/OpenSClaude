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

#include "ClaudeToolHandler.h"

#include <QMetaObject>
#include <QPlainTextEdit>
#include <QStandardItemModel>
#include <QRegularExpression>

#include "gui/MainWindow.h"
#include "gui/Editor.h"
#include "gui/Console.h"
#include "gui/ErrorLog.h"
#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#ifdef ENABLE_MANIFOLD
#include "geometry/manifold/ManifoldGeometry.h"
#endif

namespace Claude {

ToolHandler::ToolHandler(MainWindow *mainWindow, QObject *parent)
  : QObject(parent)
  , mainWindow_(mainWindow)
{
}

QJsonArray ToolHandler::getToolDefinitions() const
{
  QJsonArray tools;

  // read_editor
  {
    QJsonObject tool;
    tool["name"] = "read_editor";
    tool["description"] = "Read the current OpenSCAD code from the editor. Returns the complete source code of the currently active file.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = QJsonObject();
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // write_editor
  {
    QJsonObject tool;
    tool["name"] = "write_editor";
    tool["description"] = "Replace the entire editor content with new OpenSCAD code. This overwrites all existing code in the current file.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    QJsonObject props;
    QJsonObject contentProp;
    contentProp["type"] = "string";
    contentProp["description"] = "The complete OpenSCAD code to write to the editor";
    props["content"] = contentProp;
    inputSchema["properties"] = props;
    QJsonArray required;
    required.append("content");
    inputSchema["required"] = required;
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // get_selection
  {
    QJsonObject tool;
    tool["name"] = "get_selection";
    tool["description"] = "Get the currently selected text in the editor. Returns empty string if nothing is selected.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = QJsonObject();
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // replace_selection
  {
    QJsonObject tool;
    tool["name"] = "replace_selection";
    tool["description"] = "Replace the currently selected text with new content. If nothing is selected, inserts at the cursor position.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    QJsonObject props;
    QJsonObject contentProp;
    contentProp["type"] = "string";
    contentProp["description"] = "The text to replace the selection with";
    props["content"] = contentProp;
    inputSchema["properties"] = props;
    QJsonArray required;
    required.append("content");
    inputSchema["required"] = required;
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // insert_at_cursor
  {
    QJsonObject tool;
    tool["name"] = "insert_at_cursor";
    tool["description"] = "Insert text at the current cursor position without replacing any existing text.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    QJsonObject props;
    QJsonObject contentProp;
    contentProp["type"] = "string";
    contentProp["description"] = "The text to insert at the cursor position";
    props["content"] = contentProp;
    inputSchema["properties"] = props;
    QJsonArray required;
    required.append("content");
    inputSchema["required"] = required;
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // run_preview
  {
    QJsonObject tool;
    tool["name"] = "run_preview";
    tool["description"] = "Run the preview render (equivalent to pressing F5). This is a quick preview that shows the model faster but may not show exact geometry for complex CSG operations.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = QJsonObject();
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // run_render
  {
    QJsonObject tool;
    tool["name"] = "run_render";
    tool["description"] = "Run the full render (equivalent to pressing F6). This computes the exact geometry and is required before exporting. Takes longer than preview.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = QJsonObject();
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // get_console
  {
    QJsonObject tool;
    tool["name"] = "get_console";
    tool["description"] = "Get recent console output including compilation messages, warnings, and echo() output.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    QJsonObject props;
    QJsonObject maxLinesProp;
    maxLinesProp["type"] = "integer";
    maxLinesProp["description"] = "Maximum number of lines to return (default 100)";
    props["max_lines"] = maxLinesProp;
    inputSchema["properties"] = props;
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // get_errors
  {
    QJsonObject tool;
    tool["name"] = "get_errors";
    tool["description"] = "Get structured error log from the last compilation. Returns errors with file, line number, and message.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = QJsonObject();
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // get_file_path
  {
    QJsonObject tool;
    tool["name"] = "get_file_path";
    tool["description"] = "Get the file path of the currently active document. Returns empty if the file has not been saved.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = QJsonObject();
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // get_model_stats
  {
    QJsonObject tool;
    tool["name"] = "get_model_stats";
    tool["description"] = "Get statistics about the currently rendered 3D model including vertices, faces, and bounding box dimensions. Requires a render to have been completed first.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = QJsonObject();
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  // list_modules
  {
    QJsonObject tool;
    tool["name"] = "list_modules";
    tool["description"] = "List all module definitions in the current file. Returns module names and their parameters.";
    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = QJsonObject();
    inputSchema["required"] = QJsonArray();
    tool["input_schema"] = inputSchema;
    tools.append(tool);
  }

  return tools;
}

ToolResult ToolHandler::executeTool(const QString& toolName, const QJsonObject& input)
{
  if (toolName == "read_editor") {
    return readEditor();
  } else if (toolName == "write_editor") {
    return writeEditor(input["content"].toString());
  } else if (toolName == "get_selection") {
    return getSelection();
  } else if (toolName == "replace_selection") {
    return replaceSelection(input["content"].toString());
  } else if (toolName == "insert_at_cursor") {
    return insertAtCursor(input["content"].toString());
  } else if (toolName == "run_preview") {
    return runPreview();
  } else if (toolName == "run_render") {
    return runRender();
  } else if (toolName == "get_console") {
    int maxLines = input.contains("max_lines") ? input["max_lines"].toInt() : 100;
    return getConsole(maxLines);
  } else if (toolName == "get_errors") {
    return getErrors();
  } else if (toolName == "get_file_path") {
    return getFilePath();
  } else if (toolName == "get_model_stats") {
    return getModelStats();
  } else if (toolName == "list_modules") {
    return listModules();
  }

  return {false, QString("Unknown tool: %1").arg(toolName), true};
}

EditorInterface* ToolHandler::activeEditor() const
{
  return mainWindow_->activeEditor;
}

ToolResult ToolHandler::readEditor()
{
  EditorInterface *editor = activeEditor();
  if (!editor) {
    return {false, "No active editor", true};
  }

  QString content = editor->toPlainText();
  return {true, content, false};
}

ToolResult ToolHandler::writeEditor(const QString& content)
{
  EditorInterface *editor = activeEditor();
  if (!editor) {
    return {false, "No active editor", true};
  }

  if (content.isEmpty()) {
    return {false, "Content is empty", true};
  }

  // Use setText which handles undo properly
  editor->setText(content);
  return {true, "Editor content updated successfully", false};
}

ToolResult ToolHandler::getSelection()
{
  EditorInterface *editor = activeEditor();
  if (!editor) {
    return {false, "No active editor", true};
  }

  QString selection = editor->selectedText();
  if (selection.isEmpty()) {
    return {true, "(no text selected)", false};
  }
  return {true, selection, false};
}

ToolResult ToolHandler::replaceSelection(const QString& content)
{
  EditorInterface *editor = activeEditor();
  if (!editor) {
    return {false, "No active editor", true};
  }

  editor->replaceSelectedText(content);
  return {true, "Selection replaced successfully", false};
}

ToolResult ToolHandler::insertAtCursor(const QString& content)
{
  EditorInterface *editor = activeEditor();
  if (!editor) {
    return {false, "No active editor", true};
  }

  editor->insert(content);
  return {true, "Text inserted successfully", false};
}

ToolResult ToolHandler::runPreview()
{
  // Trigger preview render via MainWindow's action
  QMetaObject::invokeMethod(mainWindow_, "actionRenderPreview", Qt::QueuedConnection);
  return {true, "Preview render started. Check console for results.", false};
}

ToolResult ToolHandler::runRender()
{
  // Trigger full render via MainWindow
  QMetaObject::invokeMethod(mainWindow_, "actionRender", Qt::QueuedConnection);
  return {true, "Full render started. Check console for results.", false};
}

ToolResult ToolHandler::getConsole(int maxLines)
{
  Console *console = mainWindow_->findChild<Console*>("console");
  if (!console) {
    return {false, "Console not found", true};
  }

  QString consoleText = console->toPlainText();

  // Limit to maxLines
  QStringList lines = consoleText.split('\n');
  if (lines.size() > maxLines) {
    lines = lines.mid(lines.size() - maxLines);
  }

  QString result = lines.join('\n');
  if (result.isEmpty()) {
    return {true, "(console is empty)", false};
  }
  return {true, result, false};
}

ToolResult ToolHandler::getErrors()
{
  ErrorLog *errorLog = mainWindow_->findChild<ErrorLog*>("errorLogWidget");
  if (!errorLog) {
    return {false, "Error log not found", true};
  }

  // Access the model from ErrorLog
  QStandardItemModel *model = errorLog->findChild<QStandardItemModel*>();
  if (!model) {
    return {true, "(no errors)", false};
  }

  QString result;
  int rowCount = model->rowCount();
  if (rowCount == 0) {
    return {true, "(no errors)", false};
  }

  for (int i = 0; i < rowCount; ++i) {
    QString group = model->item(i, 0)->text();
    QString file = model->item(i, 1)->text();
    QString line = model->item(i, 2)->text();
    QString message = model->item(i, 3)->text();

    result += QString("[%1] %2:%3 - %4\n").arg(group, file, line, message);
  }

  return {true, result.trimmed(), false};
}

ToolResult ToolHandler::getFilePath()
{
  EditorInterface *editor = activeEditor();
  if (!editor) {
    return {false, "No active editor", true};
  }

  QString path = editor->filepath;
  if (path.isEmpty()) {
    return {true, "(unsaved file)", false};
  }
  return {true, path, false};
}

ToolResult ToolHandler::getModelStats()
{
  auto geom = mainWindow_->rootGeom;
  if (!geom) {
    return {true, "(no geometry - run preview or render first)", false};
  }

  QString result;

  // Get bounding box
  BoundingBox bbox = geom->getBoundingBox();
  if (!bbox.isEmpty()) {
    double width = bbox.max().x() - bbox.min().x();
    double depth = bbox.max().y() - bbox.min().y();
    double height = bbox.max().z() - bbox.min().z();

    result += QString("Bounding Box:\n");
    result += QString("  Min: (%.3f, %.3f, %.3f)\n").arg(bbox.min().x()).arg(bbox.min().y()).arg(bbox.min().z());
    result += QString("  Max: (%.3f, %.3f, %.3f)\n").arg(bbox.max().x()).arg(bbox.max().y()).arg(bbox.max().z());
    result += QString("  Size: %.3f x %.3f x %.3f\n").arg(width).arg(depth).arg(height);
  }

  // Get geometry-specific stats
  if (auto ps = std::dynamic_pointer_cast<const PolySet>(geom)) {
    result += QString("\nGeometry Type: PolySet\n");
    result += QString("  Triangular: %1\n").arg(ps->isTriangular() ? "yes" : "no");
    result += QString("  Convex: %1\n").arg(ps->isConvex() ? "yes" : "no");
    result += QString("  Facets: %1\n").arg(ps->numFacets());
  }
#ifdef ENABLE_MANIFOLD
  else if (auto mani = std::dynamic_pointer_cast<const ManifoldGeometry>(geom)) {
    auto& m = mani->getManifold();
    result += QString("\nGeometry Type: Manifold\n");
    result += QString("  Vertices: %1\n").arg(m.NumVert());
    result += QString("  Triangles: %1\n").arg(m.NumTri());
    result += QString("  Genus: %1\n").arg(m.Genus());
  }
#endif

  if (result.isEmpty()) {
    result = "(geometry stats not available for this type)";
  }

  return {true, result.trimmed(), false};
}

ToolResult ToolHandler::listModules()
{
  EditorInterface *editor = activeEditor();
  if (!editor) {
    return {false, "No active editor", true};
  }

  QString content = editor->toPlainText();

  // Use regex to find module definitions
  // Pattern: module name(params) { or module name() {
  QRegularExpression moduleRegex(
    R"(module\s+(\w+)\s*\(([^)]*)\))",
    QRegularExpression::MultilineOption
  );

  QRegularExpressionMatchIterator it = moduleRegex.globalMatch(content);

  QString result;
  int count = 0;

  while (it.hasNext()) {
    QRegularExpressionMatch match = it.next();
    QString moduleName = match.captured(1);
    QString params = match.captured(2).trimmed();

    if (params.isEmpty()) {
      result += QString("%1()\n").arg(moduleName);
    } else {
      // Clean up params - remove default values for cleaner output
      QStringList paramList = params.split(',');
      QStringList cleanParams;
      for (const QString& p : paramList) {
        QString param = p.trimmed();
        // Keep just parameter name if there's a default value
        int eqPos = param.indexOf('=');
        if (eqPos > 0) {
          cleanParams << param.left(eqPos).trimmed();
        } else {
          cleanParams << param;
        }
      }
      result += QString("%1(%2)\n").arg(moduleName, cleanParams.join(", "));
    }
    count++;
  }

  if (count == 0) {
    return {true, "(no modules defined)", false};
  }

  result = QString("Found %1 module(s):\n\n").arg(count) + result;
  return {true, result.trimmed(), false};
}

} // namespace Claude
