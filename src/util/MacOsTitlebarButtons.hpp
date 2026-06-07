#pragma once

class QWidget;

namespace chatterino {

class Split;
class SplitNotebook;

void setupMacOsTitlebarButtons(QWidget *window, SplitNotebook *notebook);
void setMacOsTitlebarButtonsVisible(bool visible);
void updateMacOsTitlebarButtonsForSplit(Split *split);

}  // namespace chatterino
