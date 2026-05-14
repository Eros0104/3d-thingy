#pragma once

class Window;

class Renderer {
public:
  bool init(Window &window);
  void resize(int w, int h);
  void begin_frame();
  void end_frame();
  int width() const;
  int height() const;
  ~Renderer();

  Renderer() = default;
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

private:
  int m_width = 0;
  int m_height = 0;
  bool m_initialized = false;
};
