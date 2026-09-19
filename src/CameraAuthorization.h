#pragma once

// Returns true when macOS has authorized this process to use video capture devices.
// If authorization has not been decided yet, this function presents the system prompt
// and waits for the user's response before camera discovery begins.
bool ensureCameraAuthorization();
