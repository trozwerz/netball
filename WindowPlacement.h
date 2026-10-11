#pragma once

namespace netball
{
	// Makes the game window borderless and fills the given display (1-based,
	// matching Windows' display numbering). Returns false if it could not.
	bool FillMonitor(int displayNumber);
}
