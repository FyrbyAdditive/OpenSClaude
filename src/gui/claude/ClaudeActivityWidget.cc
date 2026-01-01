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

#include "ClaudeActivityWidget.h"

namespace Claude {

ActivityWidget::ActivityWidget(QWidget *parent)
  : QWidget(parent)
  , layout_(new QVBoxLayout(this))
  , animationTimer_(new QTimer(this))
{
  layout_->setContentsMargins(8, 4, 8, 4);
  layout_->setSpacing(2);

  // Subtle background styling
  setStyleSheet(
    "Claude--ActivityWidget {"
    "  background-color: #f8f9fa;"
    "  border-top: 1px solid #e0e0e0;"
    "}"
  );

  // Animation timer for the spinner effect
  connect(animationTimer_, &QTimer::timeout, this, [this]() {
    animationFrame_ = (animationFrame_ + 1) % 3;
    if (currentToolLabel_) {
      QString dots = QString(".").repeated(animationFrame_ + 1);
      QString toolName = currentToolLabel_->property("toolName").toString();
      currentToolLabel_->setText(QString("  %1%2").arg(humanReadableStatus(toolName), dots));
    }
  });
}

void ActivityWidget::clear()
{
  animationTimer_->stop();

  // Remove all tool labels
  for (auto* label : toolLabels_) {
    layout_->removeWidget(label);
    delete label;
  }
  toolLabels_.clear();
  executedTools_.clear();
  currentToolLabel_ = nullptr;
  animationFrame_ = 0;
}

void ActivityWidget::addTool(const QString& toolName)
{
  // Stop animation on previous tool if any
  if (currentToolLabel_) {
    QString prevTool = currentToolLabel_->property("toolName").toString();
    currentToolLabel_->setText(QString("  \u2713 %1").arg(humanReadableName(prevTool)));
    currentToolLabel_->setStyleSheet("color: #2e7d32; font-size: 11px;");
  }

  // Create new label for this tool
  auto *label = new QLabel(QString("  %1...").arg(humanReadableStatus(toolName)));
  label->setProperty("toolName", toolName);
  label->setStyleSheet("color: #1565c0; font-size: 11px; font-style: italic;");

  layout_->addWidget(label);
  toolLabels_[toolName] = label;
  currentToolLabel_ = label;

  // Track executed tools for summary
  if (!executedTools_.contains(toolName)) {
    executedTools_.append(toolName);
  }

  // Start animation
  animationFrame_ = 0;
  animationTimer_->start(400);

  show();
}

void ActivityWidget::completeTool(const QString& toolName, bool success)
{
  if (!toolLabels_.contains(toolName)) return;

  QLabel *label = toolLabels_[toolName];

  if (success) {
    label->setText(QString("  \u2713 %1").arg(humanReadableName(toolName)));
    label->setStyleSheet("color: #2e7d32; font-size: 11px;");
  } else {
    label->setText(QString("  \u26A0 %1").arg(humanReadableName(toolName)));
    label->setStyleSheet("color: #e65100; font-size: 11px;");
  }

  // If this was the current tool, stop animation
  if (currentToolLabel_ == label) {
    animationTimer_->stop();
    currentToolLabel_ = nullptr;
  }
}

QString ActivityWidget::humanReadableName(const QString& toolName)
{
  static const QMap<QString, QString> names = {
    {"read_editor", "Read code"},
    {"write_editor", "Wrote code"},
    {"edit_lines", "Edited code"},
    {"search_replace", "Replaced text"},
    {"get_selection", "Checked selection"},
    {"replace_selection", "Updated selection"},
    {"insert_at_cursor", "Inserted code"},
    {"run_preview", "Ran preview"},
    {"run_render", "Rendered model"},
    {"get_console", "Checked console"},
    {"get_errors", "Checked for errors"},
    {"get_file_path", "Got file info"},
    {"get_model_stats", "Analyzed geometry"},
    {"list_modules", "Listed modules"},
  };
  return names.value(toolName, toolName);
}

QString ActivityWidget::humanReadableStatus(const QString& toolName)
{
  static const QMap<QString, QString> statuses = {
    {"read_editor", "Reading your code"},
    {"write_editor", "Writing code"},
    {"edit_lines", "Editing code"},
    {"search_replace", "Replacing text"},
    {"get_selection", "Checking selection"},
    {"replace_selection", "Updating selection"},
    {"insert_at_cursor", "Inserting code"},
    {"run_preview", "Running preview"},
    {"run_render", "Rendering model"},
    {"get_console", "Checking console"},
    {"get_errors", "Checking for errors"},
    {"get_file_path", "Getting file info"},
    {"get_model_stats", "Analyzing geometry"},
    {"list_modules", "Listing modules"},
  };
  return statuses.value(toolName, QString("Using %1").arg(toolName));
}

void ActivityWidget::updateLayout()
{
  // Adjust size based on content
  adjustSize();
}

} // namespace Claude
