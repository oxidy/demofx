## Getting Started

## Folder Structure

The demodev workspace contains several important folders and files:

- `/bin`            : folder for C64-related binaries and tools.
- `/output`         : the folder where generated C64-files will be placed.
- `/output/d64`     : the folder where generated d64-files will be placed.
- `/output/temp`    : temporary files used while building the prg-files and d64.
- `/democode`       : Your projects with C64-demos and other releases.
- `/democode/bin`   : Class-files
- `/demofx`         : The C64 Demo Framework.  

- `/settings.gradle`: gradle file with sub-projects democode and demofx
- `/config.yaml`    : Demodev configuration file.  

- `democode/src/main/java/oxidy`                         : package 
- `democode/src/main/java/oxidy/{projectname}`           : demodev project folder. 
- `democode/src/main/java/oxidy/{projectname}/includes`  : default project include files.


## Project folder

If you name the project `src/main/java/oxidy/MyDemoProject` create a demo object like this:
Demo demo = new Demo("MyDemoProject");

If you want subfolders like `src/main/java/oxidy/Released/Skaaneland2`, do this:
Demo demo = new Demo("democode/src/main/java/oxidy/Released", "Skaaneland2");


## Gradle Build

$ cd ~/demodev          // Hang out in the root folder. 
$ gradle -q projects    // View project structure.

    Root project 'demodev'
    +--- Project ':democode'
    \--- Project ':demofx'

$ gradle clean          // Clean build folders. 
$ gradle -q build       // Build project, including sub-projects.

$ jar tvf democode/build/libs/DemoLauncher-1.0.0.jar 
$ java -jar democode/build/libs/DemoLauncher-1.0.0.jar
