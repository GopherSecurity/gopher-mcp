#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <thread>
#include <chrono>

int main() {
  // Create pipes
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    std::cerr << "Failed to create pipe\n";
    return 1;
  }
  
  // Make non-blocking
  fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);
  fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK);
  
  std::cout << "Pipe created: read_fd=" << pipe_fds[0] << ", write_fd=" << pipe_fds[1] << "\n";
  
  // Write data
  const char* message = "Hello, pipe!\n";
  ssize_t written = write(pipe_fds[1], message, strlen(message));
  std::cout << "Wrote " << written << " bytes\n";
  
  // Check if data is available
  fd_set readfds;
  struct timeval tv = {0, 0};
  FD_ZERO(&readfds);
  FD_SET(pipe_fds[0], &readfds);
  
  int ready = select(pipe_fds[0] + 1, &readfds, NULL, NULL, &tv);
  std::cout << "Select says " << ready << " fds ready\n";
  
  // Read data
  char buffer[256];
  ssize_t bytes_read = read(pipe_fds[0], buffer, sizeof(buffer));
  
  if (bytes_read > 0) {
    buffer[bytes_read] = '\0';
    std::cout << "Read " << bytes_read << " bytes: " << buffer;
  } else if (bytes_read == 0) {
    std::cout << "EOF\n";
  } else {
    if (errno == EAGAIN) {
      std::cout << "No data available (EAGAIN)\n";
    } else {
      std::cout << "Read error: " << strerror(errno) << "\n";
    }
  }
  
  close(pipe_fds[0]);
  close(pipe_fds[1]);
  
  return 0;
}