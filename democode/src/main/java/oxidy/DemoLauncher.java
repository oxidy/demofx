package oxidy;

import oxidy.Samples.SpindleLoader.SpindleLoader;

public class DemoLauncher {
  public static void main(String[] args) {

    try {
      SpindleLoader.A_SpindleLoader();
      
    } catch (Exception e) {
      e.printStackTrace();
    }
  }
}