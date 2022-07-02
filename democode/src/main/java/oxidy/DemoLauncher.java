package oxidy;

import oxidy.Samples.SpindleLoader.SpindleLoader;
import demofx.Demo;

public class DemoLauncher {
  public static void main(String[] args) {

    Demo demo = new Demo("TestDemo");
	  System.out.println(demo.d64Name);

    try {
      SpindleLoader.A_SpindleLoader();
      
    } catch (Exception e) {
      e.printStackTrace();
    }
  }
}