#pragma once

class QString;
class QWidget;

namespace chatterino {

class Split;
class SplitNotebook;

void setupMacOsTitlebarButtons(QWidget *window, SplitNotebook *notebook);
void setMacOsTitlebarButtonsVisible(bool visible);
void updateMacOsTitlebarButtonsForSplit(Split *split);
void setMacOsTitlebarLabelText(const QString &text);
void setMacOsTitlebarTitleVisible(bool visible);

}  // namespace chatterino
