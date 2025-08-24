#include <iostream>
#include "mcp/event/event_loop.h"

int main() {
  std::cout << "Platform Default Trigger Type Test\n";
  std::cout << "===================================\n";
  
  auto trigger = mcp::event::PlatformDefaultTriggerType;
  
  std::cout << "Platform: ";
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
  std::cout << "Windows\n";
#elif defined(__APPLE__)
  std::cout << "macOS\n";
#elif defined(__linux__)
  std::cout << "Linux\n";
#else
  std::cout << "Unknown\n";
#endif
  
  std::cout << "Default Trigger Type: ";
  switch (trigger) {
    case mcp::event::FileTriggerType::Level:
      std::cout << "Level\n";
      break;
    case mcp::event::FileTriggerType::Edge:
      std::cout << "Edge\n";
      break;
    case mcp::event::FileTriggerType::EmulatedEdge:
      std::cout << "EmulatedEdge\n";
      break;
  }
  
  std::cout << "\nExpected behavior:\n";
  std::cout << "- Windows: EmulatedEdge (synthetic edge events)\n";
  std::cout << "- Linux: Edge (native epoll edge-triggering)\n";
  std::cout << "- macOS: Edge (native kqueue edge-triggering)\n";
  
  return 0;
}