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

#include "ClaudeWidget.h"

#include <QScrollBar>
#include <QTimer>
#include <QMessageBox>
#include <QDir>

#include "gui/MainWindow.h"
#include "gui/Editor.h"
#include "gui/ScintillaEditor.h"
#include "core/Settings.h"
#include "gui/SettingsWriter.h"

namespace Claude {

Widget::Widget(MainWindow *mainWindow, QWidget *parent)
  : QWidget(parent)
  , mainWindow_(mainWindow)
  , apiClient_(new ApiClient(this))
  , toolHandler_(new ToolHandler(mainWindow, this))
  , history_(new History(this))
{
  setupUi();
  populateModelSelector();

  // Connect API client signals
  connect(apiClient_, &ApiClient::streamStarted, this, &Widget::onStreamStarted);
  connect(apiClient_, &ApiClient::contentDelta, this, &Widget::onContentDelta);
  connect(apiClient_, &ApiClient::toolUseStarted, this, &Widget::onToolUseStarted);
  connect(apiClient_, &ApiClient::toolUseInputDelta, this, &Widget::onToolUseInputDelta);
  connect(apiClient_, &ApiClient::toolUseComplete, this, &Widget::onToolUseComplete);
  connect(apiClient_, &ApiClient::messageComplete, this, &Widget::onMessageComplete);
  connect(apiClient_, &ApiClient::errorOccurred, this, &Widget::onError);
  connect(apiClient_, &ApiClient::rateLimitWaiting, this, &Widget::onRateLimitWaiting);

  // Connect history changes
  connect(history_, &History::historyChanged, this, &Widget::onHistoryChanged);

  // Load API key from settings
  QString apiKey = QString::fromStdString(Settings::Settings::claudeApiKey.value());
  if (!apiKey.isEmpty()) {
    apiClient_->setApiKey(apiKey);
  }

  updateSendButtonState();
}

Widget::~Widget()
{
  // Save history before destruction
  if (history_ && !history_->filePath().isEmpty()) {
    history_->save();
  }
}

void Widget::setupUi()
{
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(4, 4, 4, 4);
  mainLayout->setSpacing(4);

  // Header row
  auto *headerLayout = new QHBoxLayout();
  headerLayout->setSpacing(8);

  modelSelector_ = new QComboBox();
  modelSelector_->setMinimumWidth(150);
  headerLayout->addWidget(modelSelector_);

  statusLabel_ = new QLabel("Not configured");
  statusLabel_->setStyleSheet("color: gray;");
  headerLayout->addWidget(statusLabel_);

  headerLayout->addStretch();

  settingsButton_ = new QToolButton();
  settingsButton_->setText("\u2699");  // Gear icon
  settingsButton_->setToolTip("Claude Settings");
  settingsButton_->setStyleSheet("QToolButton { font-size: 16px; }");
  connect(settingsButton_, &QToolButton::clicked, this, &Widget::onSettingsClicked);
  headerLayout->addWidget(settingsButton_);

  mainLayout->addLayout(headerLayout);

  // Chat area
  chatArea_ = new QScrollArea();
  chatArea_->setWidgetResizable(true);
  chatArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  chatArea_->setFrameShape(QFrame::StyledPanel);

  chatContainer_ = new QWidget();
  messagesLayout_ = new QVBoxLayout(chatContainer_);
  messagesLayout_->setContentsMargins(8, 8, 8, 8);
  messagesLayout_->setSpacing(8);
  messagesLayout_->addStretch();

  chatArea_->setWidget(chatContainer_);
  mainLayout->addWidget(chatArea_, 1);

  // Input area
  auto *inputLayout = new QHBoxLayout();
  inputLayout->setSpacing(4);

  inputEdit_ = new QPlainTextEdit();
  inputEdit_->setPlaceholderText("Ask Claude about your OpenSCAD model...");
  inputEdit_->setMaximumHeight(80);
  inputEdit_->setTabChangesFocus(true);
  inputLayout->addWidget(inputEdit_, 1);

  sendButton_ = new QPushButton("Send");
  sendButton_->setDefault(true);
  connect(sendButton_, &QPushButton::clicked, this, &Widget::onSendClicked);
  inputLayout->addWidget(sendButton_);

  stopButton_ = new QPushButton("Stop");
  stopButton_->setStyleSheet("QPushButton { background-color: #c0392b; color: white; }");
  stopButton_->setToolTip("Stop Claude's response");
  stopButton_->hide();  // Hidden by default, shown during streaming
  connect(stopButton_, &QPushButton::clicked, this, &Widget::onStopClicked);
  inputLayout->addWidget(stopButton_);

  mainLayout->addLayout(inputLayout);

  // Handle Enter key in input
  inputEdit_->installEventFilter(this);
}

void Widget::populateModelSelector()
{
  modelSelector_->clear();
  for (const auto& model : availableModels()) {
    modelSelector_->addItem(model.displayName, model.id);
  }

  // Set default from settings
  QString defaultModel = QString::fromStdString(Settings::Settings::claudeDefaultModel.value());
  int index = modelSelector_->findData(defaultModel);
  if (index >= 0) {
    modelSelector_->setCurrentIndex(index);
  }
}

void Widget::setEditor(EditorInterface *editor)
{
  currentEditor_ = editor;

  // Update history for new file
  QString filePath;
  if (editor) {
    filePath = editor->filepath;
  }
  history_->setFilePath(filePath);
}

void Widget::setApiKey(const QString& apiKey)
{
  apiClient_->setApiKey(apiKey);
  updateSendButtonState();

  if (apiClient_->isConfigured()) {
    statusLabel_->setText("Ready");
    statusLabel_->setStyleSheet("color: green;");
  } else {
    statusLabel_->setText("Not configured");
    statusLabel_->setStyleSheet("color: gray;");
  }
}

void Widget::onSendClicked()
{
  QString text = inputEdit_->toPlainText().trimmed();
  if (text.isEmpty()) return;

  if (!apiClient_->isConfigured()) {
    QMessageBox::warning(this, "API Key Required",
      "Please configure your Claude API key first.\n\n"
      "Click the 'API Key' button to enter your key.");
    return;
  }

  // Add user message to UI and history
  addMessageBubble("user", text);

  Message userMsg(Message::Role::User, text);
  history_->addMessage(userMsg);

  inputEdit_->clear();
  sendCurrentMessage();
}

void Widget::sendCurrentMessage()
{
  QString modelId = modelSelector_->currentData().toString();
  QJsonArray messages = history_->toApiMessages();
  QJsonArray tools = toolHandler_->getToolDefinitions();
  QString systemPrompt = getSystemPrompt();

  apiClient_->sendMessage(modelId, messages, tools, systemPrompt);

  isStreaming_ = true;
  updateSendButtonState();
  statusLabel_->setText("Thinking...");
  statusLabel_->setStyleSheet("color: blue;");
}

void Widget::onStopClicked()
{
  if (!isStreaming_) return;

  // Cancel the API request
  apiClient_->cancelRequest();

  // Finalize any streaming bubble with a note
  if (currentStreamingBubble_) {
    currentStreamingText_ += "\n\n[Stopped by user]";
    currentStreamingBubble_->setHtml(
      QString("<div style='background-color: #f0f0f0; padding: 8px; border-radius: 8px;'>"
              "<b>Claude:</b><br>%1</div>")
      .arg(currentStreamingText_.toHtmlEscaped().replace("\n", "<br>")));
    currentStreamingBubble_ = nullptr;
  }

  // Clear pending tool uses
  pendingToolUses_.clear();

  // Reset streaming state
  isStreaming_ = false;
  updateSendButtonState();
  statusLabel_->setText("Stopped");
  statusLabel_->setStyleSheet("color: orange;");
}

void Widget::onClearHistory()
{
  auto result = QMessageBox::question(this, "Clear History",
    "Are you sure you want to clear the chat history for this file?",
    QMessageBox::Yes | QMessageBox::No);

  if (result == QMessageBox::Yes) {
    history_->clear();
  }
}

void Widget::onSettingsClicked()
{
  SettingsDialog dialog(apiClient_, this);

  // Load current settings
  dialog.setApiKey(apiClient_->apiKey());
  dialog.setDefaultModel(modelSelector_->currentData().toString());
  dialog.setAutoValidate(Settings::Settings::claudeAutoValidate.value());

  // Connect clear history signal
  connect(&dialog, &SettingsDialog::clearHistoryRequested, this, [this]() {
    // Clear all history files in the claude history directory
    QString historyDir = QDir::homePath() + "/.openscad/claude_history";
    QDir dir(historyDir);
    if (dir.exists()) {
      dir.removeRecursively();
      dir.mkpath(".");
    }
    history_->clear();
  });

  if (dialog.exec() == QDialog::Accepted) {
    // Apply API key
    QString newKey = dialog.apiKey();
    if (newKey != apiClient_->apiKey()) {
      setApiKey(newKey);
      Settings::Settings::claudeApiKey.setValue(newKey.toStdString());
    }

    // Apply default model
    QString newModel = dialog.defaultModel();
    int modelIndex = modelSelector_->findData(newModel);
    if (modelIndex >= 0) {
      modelSelector_->setCurrentIndex(modelIndex);
      Settings::Settings::claudeDefaultModel.setValue(newModel.toStdString());
    }

    // Apply auto-validate setting
    Settings::Settings::claudeAutoValidate.setValue(dialog.autoValidate());

    // Persist all settings
    Settings::Settings::visit(SettingsWriter());
  }
}

void Widget::onStreamStarted()
{
  startStreamingBubble();
}

void Widget::onContentDelta(const QString& text)
{
  appendToStreamingBubble(text);
}

void Widget::onToolUseStarted(const QString& toolId, const QString& toolName)
{
  Q_UNUSED(toolId);
  currentStreamingToolName_ = toolName;
  streamingToolJson_.clear();
  lastAppliedLength_ = 0;

  if (toolName == "write_editor" || toolName == "replace_selection") {
    // Store original content for diff comparison
    if (mainWindow_->activeEditor) {
      originalContent_ = mainWindow_->activeEditor->toPlainText();
    }
  }

  appendToStreamingBubble(QString("\n[Using tool: %1...]\n").arg(toolName));
}

void Widget::onToolUseInputDelta(const QString& toolId, const QString& partialJson)
{
  Q_UNUSED(toolId);

  // Only process for write_editor and replace_selection tools
  if (currentStreamingToolName_ != "write_editor" &&
      currentStreamingToolName_ != "replace_selection") {
    return;
  }

  // Accumulate JSON and try to extract content field
  streamingToolJson_.append(partialJson);

  // Try to parse partial content from accumulated JSON
  QString partialContent = extractPartialContent(streamingToolJson_);
  if (!partialContent.isEmpty() && partialContent.length() > lastAppliedLength_) {
    applyStreamingEdit(partialContent);
    lastAppliedLength_ = partialContent.length();
  }
}

void Widget::onToolUseComplete(const QString& toolId, const QString& toolName, const QJsonObject& input)
{
  PendingToolUse pending;
  pending.toolId = toolId;
  pending.toolName = toolName;
  pending.input = input;
  pendingToolUses_.append(pending);

  // Clear streaming state
  currentStreamingToolName_.clear();
  streamingToolJson_.clear();
  originalContent_.clear();
  lastAppliedLength_ = 0;

  // Schedule highlight fade-out after 3 seconds
  QTimer::singleShot(3000, this, [this]() {
    ScintillaEditor* scintilla = qobject_cast<ScintillaEditor*>(mainWindow_->activeEditor);
    if (scintilla) {
      scintilla->clearClaudeHighlights();
    }
  });
}

void Widget::onMessageComplete(const QJsonObject& message)
{
  finalizeStreamingBubble();

  isStreaming_ = false;
  updateSendButtonState();
  statusLabel_->setText("Ready");
  statusLabel_->setStyleSheet("color: green;");

  // Extract text content from the message
  QJsonArray content = message["content"].toArray();
  QString textContent;
  bool hasToolUse = false;

  for (const QJsonValue& block : content) {
    QJsonObject blockObj = block.toObject();
    QString type = blockObj["type"].toString();

    if (type == "text") {
      textContent += blockObj["text"].toString();
    } else if (type == "tool_use") {
      hasToolUse = true;
    }
  }

  // Add assistant message to history
  if (!textContent.isEmpty()) {
    Message assistantMsg(Message::Role::Assistant, textContent);
    assistantMsg.model = modelSelector_->currentData().toString();
    history_->addMessage(assistantMsg);
  }

  // Process any pending tool uses
  if (!pendingToolUses_.isEmpty()) {
    for (const auto& pending : pendingToolUses_) {
      // Add tool use to history
      Message toolUseMsg;
      toolUseMsg.role = Message::Role::ToolUse;
      toolUseMsg.toolId = pending.toolId;
      toolUseMsg.toolName = pending.toolName;
      toolUseMsg.toolInput = pending.input;
      history_->addMessage(toolUseMsg);

      // Execute the tool
      processToolUse(pending.toolId, pending.toolName, pending.input);
    }
    pendingToolUses_.clear();

    // Continue the conversation with tool results
    sendCurrentMessage();
  } else {
    // No tool use, save history
    history_->save();
  }
}

void Widget::onError(const QString& error)
{
  isStreaming_ = false;
  updateSendButtonState();

  statusLabel_->setText("Error");
  statusLabel_->setStyleSheet("color: red;");

  addMessageBubble("error", "Error: " + error);
}

void Widget::onRateLimitWaiting(int secondsRemaining)
{
  statusLabel_->setText(QString("Rate limited - retrying in %1s...").arg(secondsRemaining));
  statusLabel_->setStyleSheet("color: orange;");

  // Add info message to chat
  addMessageBubble("tool-result", QString("Rate limited by API. Automatically retrying in %1 seconds...").arg(secondsRemaining));
}

void Widget::onHistoryChanged()
{
  // Rebuild the chat display from history
  // Remove all message bubbles except the stretch
  while (messagesLayout_->count() > 1) {
    QLayoutItem *item = messagesLayout_->takeAt(0);
    if (item->widget()) {
      item->widget()->deleteLater();
    }
    delete item;
  }

  // Add messages from history
  for (const Message& msg : history_->messages()) {
    QString role;
    QString content = msg.content;

    switch (msg.role) {
      case Message::Role::User:
        role = "user";
        break;
      case Message::Role::Assistant:
        role = "assistant";
        break;
      case Message::Role::ToolUse:
        role = "tool";
        content = QString("[Tool: %1]").arg(msg.toolName);
        break;
      case Message::Role::ToolResult:
        role = "tool-result";
        content = QString("[Result: %1]").arg(msg.content.left(100));
        break;
    }

    if (!content.isEmpty()) {
      addMessageBubble(role, content);
    }
  }

  scrollToBottom();
}

void Widget::updateSendButtonState()
{
  bool canSend = apiClient_->isConfigured() && !isStreaming_;
  sendButton_->setEnabled(canSend);
  sendButton_->setVisible(!isStreaming_);
  stopButton_->setVisible(isStreaming_);
  inputEdit_->setEnabled(!isStreaming_);
}

void Widget::scrollToBottom()
{
  QTimer::singleShot(10, [this]() {
    QScrollBar *scrollBar = chatArea_->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
  });
}

QWidget* Widget::createMessageBubble(const QString& role, const QString& content)
{
  auto *bubble = new QTextBrowser();
  bubble->setOpenExternalLinks(true);
  bubble->setReadOnly(true);
  bubble->setFrameShape(QFrame::NoFrame);

  // Disable scroll bars - let the bubble expand to fit content
  bubble->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  bubble->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // Style based on role
  QString bgColor, textColor;
  if (role == "user") {
    bgColor = "#e3f2fd";
    textColor = "#1565c0";
  } else if (role == "assistant") {
    bgColor = "#f5f5f5";
    textColor = "#212121";
  } else if (role == "error") {
    bgColor = "#ffebee";
    textColor = "#c62828";
  } else {
    bgColor = "#fff3e0";
    textColor = "#e65100";
  }

  bubble->setStyleSheet(QString(
    "QTextBrowser {"
    "  background-color: %1;"
    "  color: %2;"
    "  border-radius: 8px;"
    "  padding: 8px;"
    "}"
  ).arg(bgColor, textColor));

  bubble->setPlainText(content);

  // Calculate height based on document size
  bubble->document()->setTextWidth(bubble->viewport()->width());
  int docHeight = bubble->document()->size().height();
  int contentHeight = docHeight + 20;  // Add padding

  // Set fixed height based on content (no scroll bars needed)
  bubble->setFixedHeight(contentHeight);

  return bubble;
}

void Widget::addMessageBubble(const QString& role, const QString& content)
{
  QWidget *bubble = createMessageBubble(role, content);

  // Insert before the stretch
  int insertIndex = messagesLayout_->count() - 1;
  messagesLayout_->insertWidget(insertIndex, bubble);

  scrollToBottom();
}

void Widget::startStreamingBubble()
{
  currentStreamingBubble_ = new QTextBrowser();
  currentStreamingBubble_->setOpenExternalLinks(true);
  currentStreamingBubble_->setReadOnly(true);
  currentStreamingBubble_->setFrameShape(QFrame::NoFrame);

  // Disable scroll bars - let the bubble expand to fit content
  currentStreamingBubble_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  currentStreamingBubble_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  currentStreamingBubble_->setStyleSheet(
    "QTextBrowser {"
    "  background-color: #f5f5f5;"
    "  color: #212121;"
    "  border-radius: 8px;"
    "  padding: 8px;"
    "}"
  );
  currentStreamingBubble_->setMinimumHeight(40);

  currentStreamingText_.clear();

  int insertIndex = messagesLayout_->count() - 1;
  messagesLayout_->insertWidget(insertIndex, currentStreamingBubble_);

  scrollToBottom();
}

void Widget::appendToStreamingBubble(const QString& text)
{
  if (!currentStreamingBubble_) return;

  currentStreamingText_.append(text);
  currentStreamingBubble_->setPlainText(currentStreamingText_);

  // Calculate height based on document size
  currentStreamingBubble_->document()->setTextWidth(currentStreamingBubble_->viewport()->width());
  int docHeight = currentStreamingBubble_->document()->size().height();
  int contentHeight = docHeight + 20;  // Add padding

  // Update height to fit content
  currentStreamingBubble_->setFixedHeight(qMax(40, contentHeight));

  scrollToBottom();
}

void Widget::finalizeStreamingBubble()
{
  currentStreamingBubble_ = nullptr;
  currentStreamingText_.clear();
}

void Widget::addToolUseBubble(const QString& toolName, const QString& result)
{
  QString content = QString("[%1]\n%2").arg(toolName, result.left(200));
  addMessageBubble("tool-result", content);
}

void Widget::processToolUse(const QString& toolId, const QString& toolName, const QJsonObject& input)
{
  qDebug() << "Claude: Processing tool" << toolName << "with input keys:" << input.keys();
  ToolResult result = toolHandler_->executeTool(toolName, input);
  qDebug() << "Claude: Tool result - success:" << result.success << "error:" << result.isError;

  // Add tool result to history
  Message toolResultMsg;
  toolResultMsg.role = Message::Role::ToolResult;
  toolResultMsg.toolId = toolId;
  toolResultMsg.content = result.content;
  toolResultMsg.isError = result.isError;
  history_->addMessage(toolResultMsg);

  // Show tool result in UI
  addToolUseBubble(toolName, result.content);
}

void Widget::sendToolResult(const QString& toolId, const QString& result, bool isError)
{
  Q_UNUSED(toolId);
  Q_UNUSED(result);
  Q_UNUSED(isError);
  // Tool results are now handled in processToolUse and sent via sendCurrentMessage
}

QString Widget::getSystemPrompt() const
{
  return QString(
    "You are an AI assistant integrated into OpenSCAD, a 3D CAD modeling application. "
    "You help users create, modify, and debug OpenSCAD code.\n\n"

    "## OpenSCAD Language Overview\n"
    "OpenSCAD uses a functional programming language for creating 3D models through "
    "constructive solid geometry (CSG).\n\n"

    "### Primitives\n"
    "- cube([x,y,z]) or cube(size, center=false)\n"
    "- sphere(r) or sphere(d=diameter)\n"
    "- cylinder(h, r1, r2) or cylinder(h, r, center=false)\n"
    "- polyhedron(points, faces)\n\n"

    "### Transformations\n"
    "- translate([x,y,z])\n"
    "- rotate([x,y,z]) or rotate(a, v=[x,y,z])\n"
    "- scale([x,y,z])\n"
    "- mirror([x,y,z])\n"
    "- multmatrix(m) - 4x4 transformation matrix\n"
    "- color(\"name\") or color([r,g,b,a])\n"
    "- resize([x,y,z], auto=false)\n\n"

    "### Boolean Operations\n"
    "- union() { ... } - combine objects\n"
    "- difference() { ... } - subtract subsequent objects from first\n"
    "- intersection() { ... } - keep only overlapping regions\n\n"

    "### 2D Shapes & Extrusions\n"
    "- circle(r) or circle(d=diameter)\n"
    "- square([x,y], center=false)\n"
    "- polygon(points, paths)\n"
    "- text(\"string\", size, font)\n"
    "- linear_extrude(height, twist, slices, scale) { 2D... }\n"
    "- rotate_extrude(angle) { 2D... }\n\n"

    "### Advanced Operations\n"
    "- hull() { ... } - convex hull of children\n"
    "- minkowski() { ... } - Minkowski sum of children\n"
    "- offset(r) or offset(delta) - expand/contract 2D shapes\n"
    "- projection(cut=false) - 3D to 2D projection\n\n"

    "### Resolution Control\n"
    "- $fn = number of fragments (overrides $fa and $fs)\n"
    "- $fa = minimum angle per fragment\n"
    "- $fs = minimum size per fragment\n\n"

    "### Modules & Functions\n"
    "- module name(params) { ... } - reusable geometry\n"
    "- function name(params) = expression; - reusable calculations\n"
    "- children() - access child geometry in modules\n"
    "- include <file.scad> - include and execute\n"
    "- use <file.scad> - import modules/functions only\n\n"

    "## Common Errors & Solutions\n"
    "- 'Object isn't defined yet': Variable used before assignment in same scope\n"
    "- 'WARNING: Normalized mesh': Usually harmless, indicates mesh cleanup\n"
    "- 'No top level geometry': Code has no rendered objects\n"
    "- '$fn too small': Use $fn >= 3 for valid geometry\n\n"

    "## Your Tools\n"
    "Editor tools:\n"
    "- read_editor: Read current code\n"
    "- write_editor: Replace all content (use for large changes)\n"
    "- edit_lines: Replace specific line range (use for targeted edits)\n"
    "- search_replace: Find and replace text\n"
    "- get_selection: Get selected text\n"
    "- replace_selection: Replace selected text\n"
    "- insert_at_cursor: Insert at cursor\n\n"

    "Compilation tools:\n"
    "- run_preview: Quick F5 preview\n"
    "- run_render: Full F6 render\n"
    "- get_console: Console output with messages\n"
    "- get_errors: Structured error log\n\n"

    "Context tools:\n"
    "- get_file_path: Current file path\n"
    "- get_model_stats: Geometry info (vertices, bounding box)\n"
    "- list_modules: List defined modules\n\n"

    "## CRITICAL WORKFLOW\n"
    "1. Read the current code first (read_editor) to understand context\n"
    "2. Make your changes (prefer edit_lines for small changes, write_editor for large ones)\n"
    "3. Run preview (run_preview) to compile\n"
    "4. ALWAYS check for errors immediately (get_errors)\n"
    "5. If errors exist, fix them and repeat steps 2-4\n"
    "6. Only report success when code compiles without errors\n\n"

    "NEVER leave the user with broken code. If your changes cause errors, fix them before finishing."
  );
}

void Widget::applyStreamingEdit(const QString& content)
{
  EditorInterface* editor = mainWindow_->activeEditor;
  if (!editor) return;

  ScintillaEditor* scintilla = qobject_cast<ScintillaEditor*>(editor);
  if (!scintilla) return;

  // Check that the scintilla widget is properly initialized
  if (!scintilla->qsci) return;

  // Apply the content
  editor->setText(content);

  // Calculate diff and highlight additions
  int newLines = content.count('\n');

  // Find where content starts to differ
  int firstDiffLine = findFirstDifferentLine(originalContent_, content);
  if (firstDiffLine >= 0) {
    // Clear previous highlights
    scintilla->clearClaudeHighlights();

    // Add margin markers for new/changed lines
    for (int i = firstDiffLine; i <= newLines; ++i) {
      scintilla->highlightClaudeAddition(i);
    }

    // Scroll to show the new content at the bottom of the visible area
    scintilla->scrollToLine(newLines);
  }
}

QString Widget::extractPartialContent(const QString& partialJson)
{
  // Tool input JSON looks like: {"content": "...code here..."}
  // We want to extract content even if JSON is incomplete

  int contentStart = partialJson.indexOf("\"content\"");
  if (contentStart < 0) return QString();

  int colonPos = partialJson.indexOf(':', contentStart);
  if (colonPos < 0) return QString();

  int quoteStart = partialJson.indexOf('"', colonPos);
  if (quoteStart < 0) return QString();

  // Find matching end quote (handling JSON escapes)
  QString content;
  bool escaped = false;
  for (int i = quoteStart + 1; i < partialJson.length(); ++i) {
    QChar c = partialJson[i];
    if (escaped) {
      if (c == 'n') content += '\n';
      else if (c == 't') content += '\t';
      else if (c == 'r') content += '\r';
      else if (c == '\\') content += '\\';
      else if (c == '"') content += '"';
      else if (c == '/') content += '/';
      else content += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      break;  // End of string
    } else {
      content += c;
    }
  }

  return content;
}

int Widget::findFirstDifferentLine(const QString& original, const QString& modified)
{
  QStringList origLines = original.split('\n');
  QStringList modLines = modified.split('\n');

  int minLines = qMin(origLines.size(), modLines.size());

  for (int i = 0; i < minLines; ++i) {
    if (origLines[i] != modLines[i]) {
      return i;
    }
  }

  // If all common lines match, difference starts at the first new line
  if (modLines.size() > origLines.size()) {
    return origLines.size();
  }

  return -1;  // No difference found
}

} // namespace Claude
