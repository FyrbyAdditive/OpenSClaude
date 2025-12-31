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
#include <QInputDialog>

#include "gui/MainWindow.h"
#include "gui/Editor.h"
#include "core/Settings.h"

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
  connect(apiClient_, &ApiClient::toolUseComplete, this, &Widget::onToolUseComplete);
  connect(apiClient_, &ApiClient::messageComplete, this, &Widget::onMessageComplete);
  connect(apiClient_, &ApiClient::errorOccurred, this, &Widget::onError);

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

  clearButton_ = new QToolButton();
  clearButton_->setText("Clear");
  clearButton_->setToolTip("Clear chat history");
  connect(clearButton_, &QToolButton::clicked, this, &Widget::onClearHistory);
  headerLayout->addWidget(clearButton_);

  settingsButton_ = new QToolButton();
  settingsButton_->setText("API Key");
  settingsButton_->setToolTip("Configure API key");
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
  bool ok;
  QString currentKey = apiClient_->apiKey();
  QString maskedKey = currentKey.isEmpty() ? "" : QString(currentKey.length(), '*');

  QString apiKey = QInputDialog::getText(this, "Claude API Key",
    "Enter your Anthropic API key:\n(Get one at console.anthropic.com)",
    QLineEdit::Password, currentKey, &ok);

  if (ok && !apiKey.isEmpty()) {
    setApiKey(apiKey);
    // Save to settings
    Settings::Settings::claudeApiKey.setValue(apiKey.toStdString());
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
  appendToStreamingBubble(QString("\n[Using tool: %1...]\n").arg(toolName));
}

void Widget::onToolUseComplete(const QString& toolId, const QString& toolName, const QJsonObject& input)
{
  PendingToolUse pending;
  pending.toolId = toolId;
  pending.toolName = toolName;
  pending.input = input;
  pendingToolUses_.append(pending);
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

  // Style based on role
  QString bgColor, textColor, alignment;
  if (role == "user") {
    bgColor = "#e3f2fd";
    textColor = "#1565c0";
    alignment = "right";
  } else if (role == "assistant") {
    bgColor = "#f5f5f5";
    textColor = "#212121";
    alignment = "left";
  } else if (role == "error") {
    bgColor = "#ffebee";
    textColor = "#c62828";
    alignment = "left";
  } else {
    bgColor = "#fff3e0";
    textColor = "#e65100";
    alignment = "left";
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

  // Adjust height to content
  QFontMetrics fm(bubble->font());
  int lineCount = content.count('\n') + 1;
  int textHeight = fm.lineSpacing() * lineCount + 24;
  bubble->setMinimumHeight(qMin(textHeight, 200));
  bubble->setMaximumHeight(400);

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
  currentStreamingBubble_->setStyleSheet(
    "QTextBrowser {"
    "  background-color: #f5f5f5;"
    "  color: #212121;"
    "  border-radius: 8px;"
    "  padding: 8px;"
    "}"
  );
  currentStreamingBubble_->setMinimumHeight(40);
  currentStreamingBubble_->setMaximumHeight(400);

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

  // Adjust height
  QFontMetrics fm(currentStreamingBubble_->font());
  int lineCount = currentStreamingText_.count('\n') + 1;
  int textHeight = fm.lineSpacing() * lineCount + 24;
  currentStreamingBubble_->setMinimumHeight(qMin(textHeight, 200));

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
  ToolResult result = toolHandler_->executeTool(toolName, input);

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
    "OpenSCAD uses a functional programming language for creating 3D models through "
    "constructive solid geometry (CSG). Key concepts include:\n"
    "- Primitives: cube(), sphere(), cylinder(), polyhedron()\n"
    "- Transformations: translate(), rotate(), scale(), mirror()\n"
    "- Boolean operations: union(), difference(), intersection()\n"
    "- 2D shapes: circle(), square(), polygon(), text()\n"
    "- Extrusions: linear_extrude(), rotate_extrude()\n"
    "- Modules and functions for code reuse\n"
    "- Special variables: $fn, $fa, $fs for resolution control\n\n"
    "You have tools to:\n"
    "- Read and modify the code in the editor\n"
    "- Run preview (F5) and full render (F6)\n"
    "- Check console output and error logs\n\n"
    "When helping users:\n"
    "1. Read the current code first to understand context\n"
    "2. Make targeted changes rather than rewriting everything\n"
    "3. Run preview to check for errors after changes\n"
    "4. Explain what you're doing and why\n"
    "5. Use proper OpenSCAD syntax and best practices"
  );
}

} // namespace Claude
