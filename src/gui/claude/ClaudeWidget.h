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
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QScrollArea>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>

#include "ClaudeApiClient.h"
#include "ClaudeToolHandler.h"
#include "ClaudeHistory.h"
#include "ClaudeMessage.h"

class MainWindow;
class EditorInterface;

namespace Claude {

class Widget : public QWidget
{
  Q_OBJECT

public:
  explicit Widget(MainWindow *mainWindow, QWidget *parent = nullptr);
  ~Widget() override;

  void setEditor(EditorInterface *editor);
  void setApiKey(const QString& apiKey);

public slots:
  void onSendClicked();
  void onClearHistory();
  void onSettingsClicked();

private slots:
  void onStreamStarted();
  void onContentDelta(const QString& text);
  void onToolUseStarted(const QString& toolId, const QString& toolName);
  void onToolUseInputDelta(const QString& toolId, const QString& partialJson);
  void onToolUseComplete(const QString& toolId, const QString& toolName, const QJsonObject& input);
  void onMessageComplete(const QJsonObject& message);
  void onError(const QString& error);
  void onRateLimitWaiting(int secondsRemaining);
  void onHistoryChanged();

private:
  void setupUi();
  void populateModelSelector();
  void updateSendButtonState();
  void scrollToBottom();

  QWidget* createMessageBubble(const QString& role, const QString& content);
  void addMessageBubble(const QString& role, const QString& content);
  void startStreamingBubble();
  void appendToStreamingBubble(const QString& text);
  void finalizeStreamingBubble();
  void addToolUseBubble(const QString& toolName, const QString& result);

  void sendCurrentMessage();
  void processToolUse(const QString& toolId, const QString& toolName, const QJsonObject& input);
  void sendToolResult(const QString& toolId, const QString& result, bool isError);

  // Streaming edit helpers
  void applyStreamingEdit(const QString& content);
  QString extractPartialContent(const QString& partialJson);
  int findFirstDifferentLine(const QString& original, const QString& modified);

  QString getSystemPrompt() const;

  MainWindow *mainWindow_;
  EditorInterface *currentEditor_{nullptr};

  // UI components
  QComboBox *modelSelector_;
  QLabel *statusLabel_;
  QToolButton *settingsButton_;
  QToolButton *clearButton_;
  QScrollArea *chatArea_;
  QWidget *chatContainer_;
  QVBoxLayout *messagesLayout_;
  QPlainTextEdit *inputEdit_;
  QPushButton *sendButton_;

  // Backend components
  ApiClient *apiClient_;
  ToolHandler *toolHandler_;
  History *history_;

  // Streaming state
  QTextBrowser *currentStreamingBubble_{nullptr};
  QString currentStreamingText_;
  bool isStreaming_{false};

  // Pending tool uses to process
  struct PendingToolUse {
    QString toolId;
    QString toolName;
    QJsonObject input;
  };
  QVector<PendingToolUse> pendingToolUses_;

  // Streaming edit state
  QString currentStreamingToolName_;
  QString streamingToolJson_;
  QString originalContent_;
  int lastAppliedLength_{0};
  int editStartLine_{0};
};

} // namespace Claude
