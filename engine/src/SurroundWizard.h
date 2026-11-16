#pragma once

// Native STR-K900 / OHL 5.1 diagnostic window. It talks to the running engine over the existing
// named pipe and never opens the audio endpoint itself.
int RunSurroundWizardGui();
