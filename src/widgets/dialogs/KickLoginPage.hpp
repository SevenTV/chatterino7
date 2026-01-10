#pragma once

#include <QLineEdit>
#include <QWidget>

namespace chatterino {

class KickLoginPage : public QWidget
{
public:
    KickLoginPage();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct {
        QLineEdit *clientID = nullptr;
        QLineEdit *clientSecret = nullptr;
    } ui;
};

}  // namespace chatterino
