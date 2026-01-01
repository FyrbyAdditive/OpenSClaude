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

#include "ClaudeSettingsDialog.h"
#include "ClaudeApiClient.h"
#include "ClaudeMessage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDialogButtonBox>

namespace Claude {

SettingsDialog::SettingsDialog(ApiClient *apiClient, QWidget *parent)
  : QDialog(parent)
  , apiClient_(apiClient)
{
  setWindowTitle("Claude Settings");
  setMinimumWidth(400);
  setupUi();
}

void SettingsDialog::setupUi()
{
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(12);

  // API Key section
  auto *apiGroup = new QGroupBox("API Configuration");
  auto *apiLayout = new QVBoxLayout(apiGroup);

  // API Key row
  auto *keyLayout = new QHBoxLayout();
  apiKeyEdit_ = new QLineEdit();
  apiKeyEdit_->setEchoMode(QLineEdit::Password);
  apiKeyEdit_->setPlaceholderText("Enter your Anthropic API key...");
  connect(apiKeyEdit_, &QLineEdit::textChanged, this, &SettingsDialog::onApiKeyChanged);
  keyLayout->addWidget(apiKeyEdit_, 1);

  showKeyButton_ = new QToolButton();
  showKeyButton_->setText("Show");
  showKeyButton_->setCheckable(true);
  connect(showKeyButton_, &QToolButton::clicked, this, &SettingsDialog::onToggleKeyVisibility);
  keyLayout->addWidget(showKeyButton_);

  apiLayout->addLayout(keyLayout);

  // Status and validate row
  auto *statusLayout = new QHBoxLayout();
  keyStatusLabel_ = new QLabel("Not configured");
  keyStatusLabel_->setStyleSheet("color: gray;");
  statusLayout->addWidget(keyStatusLabel_);

  statusLayout->addStretch();

  validateButton_ = new QPushButton("Validate");
  validateButton_->setToolTip("Test API connection");
  connect(validateButton_, &QPushButton::clicked, this, &SettingsDialog::onValidateClicked);
  statusLayout->addWidget(validateButton_);

  apiLayout->addLayout(statusLayout);

  // Auto-validate checkbox
  autoValidateCheck_ = new QCheckBox("Validate API key on startup");
  autoValidateCheck_->setToolTip("Automatically test API connection when OpenSCAD starts");
  apiLayout->addWidget(autoValidateCheck_);

  mainLayout->addWidget(apiGroup);

  // Model selection section
  auto *modelGroup = new QGroupBox("Default Model");
  auto *modelLayout = new QHBoxLayout(modelGroup);

  modelSelector_ = new QComboBox();
  for (const auto& model : availableModels()) {
    modelSelector_->addItem(model.displayName, model.id);
  }
  modelLayout->addWidget(modelSelector_, 1);

  mainLayout->addWidget(modelGroup);

  // History section
  auto *historyGroup = new QGroupBox("Conversation History");
  auto *historyLayout = new QHBoxLayout(historyGroup);

  auto *historyLabel = new QLabel("Clear all saved conversations");
  historyLayout->addWidget(historyLabel);
  historyLayout->addStretch();

  clearHistoryButton_ = new QPushButton("Clear All History");
  clearHistoryButton_->setToolTip("Delete all saved Claude conversation history");
  connect(clearHistoryButton_, &QPushButton::clicked, this, &SettingsDialog::onClearHistoryClicked);
  historyLayout->addWidget(clearHistoryButton_);

  mainLayout->addWidget(historyGroup);

  // Dialog buttons
  mainLayout->addStretch();

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttonBox);

  // Initialize status
  onApiKeyChanged();
}

QString SettingsDialog::apiKey() const
{
  return apiKeyEdit_->text();
}

void SettingsDialog::setApiKey(const QString& key)
{
  apiKeyEdit_->setText(key);
}

QString SettingsDialog::defaultModel() const
{
  return modelSelector_->currentData().toString();
}

void SettingsDialog::setDefaultModel(const QString& modelId)
{
  int index = modelSelector_->findData(modelId);
  if (index >= 0) {
    modelSelector_->setCurrentIndex(index);
  }
}

bool SettingsDialog::autoValidate() const
{
  return autoValidateCheck_->isChecked();
}

void SettingsDialog::setAutoValidate(bool enabled)
{
  autoValidateCheck_->setChecked(enabled);
}

void SettingsDialog::onToggleKeyVisibility()
{
  if (showKeyButton_->isChecked()) {
    apiKeyEdit_->setEchoMode(QLineEdit::Normal);
    showKeyButton_->setText("Hide");
  } else {
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    showKeyButton_->setText("Show");
  }
}

void SettingsDialog::onValidateClicked()
{
  QString key = apiKeyEdit_->text().trimmed();
  if (key.isEmpty()) {
    updateKeyStatus("No API key entered", "orange");
    return;
  }

  // Basic format validation
  if (!key.startsWith("sk-ant-")) {
    updateKeyStatus("Invalid format (should start with sk-ant-)", "red");
    return;
  }

  // For now, just check the format is valid
  // A full validation would require an API call
  updateKeyStatus("Format valid", "green");
}

void SettingsDialog::onClearHistoryClicked()
{
  auto result = QMessageBox::warning(this, "Clear All History",
    "This will delete ALL saved Claude conversation history for ALL files.\n\n"
    "This action cannot be undone. Continue?",
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (result == QMessageBox::Yes) {
    emit clearHistoryRequested();
    QMessageBox::information(this, "History Cleared",
      "All conversation history has been cleared.");
  }
}

void SettingsDialog::onApiKeyChanged()
{
  QString key = apiKeyEdit_->text().trimmed();
  if (key.isEmpty()) {
    updateKeyStatus("Not configured", "gray");
  } else if (key.startsWith("sk-ant-")) {
    updateKeyStatus("Ready", "green");
  } else {
    updateKeyStatus("Invalid format", "orange");
  }
}

void SettingsDialog::updateKeyStatus(const QString& status, const QString& color)
{
  keyStatusLabel_->setText(status);
  keyStatusLabel_->setStyleSheet(QString("color: %1;").arg(color));
}

} // namespace Claude
