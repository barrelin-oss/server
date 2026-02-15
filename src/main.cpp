// main.cpp
// Cross-platform console entry point for Helbreath server
// Replaces the Windows-specific WinMain in Wmain.cpp

#include "application.h"

#include <iostream>

int main(int argc, char* argv[])
{
    return hb::application::instance().run(argc, argv);
}
