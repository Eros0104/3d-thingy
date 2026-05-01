#pragma once

class Application {
public:
  bool init();
  ~Application();

  Application() = default;
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

private:
  bool m_initialized = false;
};
