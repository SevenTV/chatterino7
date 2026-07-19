#pragma once

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
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
        QFormLayout *layout = nullptr;
        QLabel *topLabel = nullptr;
        QLineEdit *clientID = nullptr;
        QLineEdit *clientSecret = nullptr;
        QComboBox *methodCombo = nullptr;
    } ui;

    void refreshState() const;
};

}  // namespace chatterino
