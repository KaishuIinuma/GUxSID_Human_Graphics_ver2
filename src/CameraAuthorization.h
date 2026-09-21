#pragma once

#include <string>

// Returns true when macOS has authorized this process to use video capture devices.
// If authorization has not been decided yet, this function presents the system prompt
// and waits for the user's response before camera discovery begins.
bool ensureCameraAuthorization();

// Creates an uncompressed ARGB QuickTime movie from frame_XXXXXX.png files.
// Returns an empty string on success, or an error description on failure.
std::string createLosslessMovieWithAVFoundation(
    const std::string &pngDirectory,
    const std::string &moviePath,
    int width,
    int height,
    int frameCount,
    double frameRate);
