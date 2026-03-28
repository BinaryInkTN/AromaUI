
<img src="getting-started/project_creation.png"/>


## 1. Project Creation via Command Line Tool

To create a new AromaUI project, you can use the `aroma` command-line tool. Open your terminal and run the following command:

```bash
aroma create
```

This command will prompt you to enter a project name and select the min, target and compile android API versions.

After providing the necessary information, the tool will generate a new project with the appropriate structure and configuration files. 

## 2. Project Structure Overview

A typical AromaUI project contains:

```
project/
 ├── src/
 │    └── main.c
 ├── android/
 ├── CMakeLists.txt
 └── aroma.json
```

Your application logic is written in C inside `src/`.

