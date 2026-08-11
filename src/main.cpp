import window;

int main(int, char**){
    Window window(800, 600, "window");
    window.init();
    window.renderLoop([](){
      for (;;){

      }
    });
}
