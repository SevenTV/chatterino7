// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/AccountSwitchPopup.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "singletons/Theme.hpp"
#include "widgets/AccountSwitchWidget.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/helper/KickAccountSwitchWidget.hpp"
#include "widgets/helper/MicroNotebook.hpp"

#include <QLayout>
#include <QPainter>
#include <QPushButton>

namespace chatterino {

using namespace literals;

AccountSwitchPopup::AccountSwitchPopup(QWidget *parent)
    : BaseWindow(
          {
              BaseWindow::TopMost,
              BaseWindow::Frameless,
              BaseWindow::DisableLayoutSave,
              BaseWindow::LinuxPopup,
          },
          parent)
{
    this->focusOutAction = FocusOutAction::Hide;

    this->setContentsMargins(0, 0, 0, 0);

    auto *notebook = new MicroNotebook(this);

    this->ui_.accountSwitchWidget = new AccountSwitchWidget(this);
    this->ui_.accountSwitchWidget->setFocusPolicy(Qt::NoFocus);
    this->ui_.kickAccountSwitcher = new KickAccountSwitchWidget(this);
    this->ui_.kickAccountSwitcher->setFocusPolicy(Qt::NoFocus);

    auto updateNotebook = [this, notebook] {
        if (getApp()->getAccounts()->kick.accounts.empty())
        {
            notebook->setShowHeader(false);
            notebook->select(this->ui_.accountSwitchWidget);
        }
        else
        {
            notebook->setShowHeader(true);
        }
    };
    updateNotebook();
    this->signalHolder_.addConnection(
        getApp()->getAccounts()->kick.userListUpdated.connect(updateNotebook));

    notebook->addPage(this->ui_.accountSwitchWidget, "Twitch");
    notebook->addPage(this->ui_.kickAccountSwitcher, "Kick");
    QVBoxLayout *vbox = new QVBoxLayout(this);
    vbox->addWidget(notebook);

    auto *hbox = new QHBoxLayout();
    auto *manageAccountsButton = new QPushButton(this);
    manageAccountsButton->setText("Manage Accounts");
    manageAccountsButton->setFocusPolicy(Qt::NoFocus);
    hbox->addWidget(manageAccountsButton);
    vbox->addLayout(hbox);

    connect(manageAccountsButton, &QPushButton::clicked, [this]() {
        SettingsDialog::showDialog(this->parentWidget(),
                                   SettingsDialogPreference::Accounts);
    });

    this->getLayoutContainer()->setLayout(vbox);

    this->setScaleIndependentSize(200, 200);
    this->themeChangedEvent();
}

void AccountSwitchPopup::themeChangedEvent()
{
    BaseWindow::themeChangedEvent();

    auto *t = getTheme();
    auto color = [](const QColor &c) {
        return c.name(QColor::HexArgb);
    };
    this->setStyleSheet(uR"(
        QListView {
            color: %1;
            background: %2;
        }
        QListView::item:hover {
            background: %3;
        }
        QListView::item:selected {
            background: %4;
        }

        QPushButton {
            background: %5;
            color: %1;
        }
        QPushButton:hover {
            background: %3;
        }
        QPushButton:pressed {
            background: %6;
        }

        chatterino--AccountSwitchPopup {
            background: %7;
        }
    )"_s.arg(color(t->window.text), color(t->splits.header.background),
             color(t->splits.header.focusedBackground), color(t->accent),
             color(t->tabs.regular.backgrounds.regular),
             color(t->tabs.selected.backgrounds.regular),
             color(t->window.background)));
}

void AccountSwitchPopup::refresh()
{
    this->ui_.accountSwitchWidget->refresh();
    this->ui_.kickAccountSwitcher->refresh();
}

void AccountSwitchPopup::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    painter.setPen(QColor("#999"));
    painter.drawRect(0, 0, this->width() - 1, this->height() - 1);
}

}  // namespace chatterino
