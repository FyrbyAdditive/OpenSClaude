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

#include <QDialog>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>

namespace Claude {

class ApiClient;

class SettingsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit SettingsDialog(ApiClient *apiClient, QWidget *parent = nullptr);

  QString apiKey() const;
  void setApiKey(const QString& key);

  QString defaultModel() const;
  void setDefaultModel(const QString& modelId);

  bool autoValidate() const;
  void setAutoValidate(bool enabled);

signals:
  void clearHistoryRequested();

private slots:
  void onToggleKeyVisibility();
  void onValidateClicked();
  void onClearHistoryClicked();
  void onApiKeyChanged();

private:
  void setupUi();
  void updateKeyStatus(const QString& status, const QString& color);

  ApiClient *apiClient_;

  QLineEdit *apiKeyEdit_;
  QToolButton *showKeyButton_;
  QLabel *keyStatusLabel_;
  QComboBox *modelSelector_;
  QCheckBox *autoValidateCheck_;
  QPushButton *validateButton_;
  QPushButton *clearHistoryButton_;
};

} // namespace Claude
