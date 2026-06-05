# File Path Resolution
## Exercise7 Slide
[OSC2026_lab7_exercise](https://docs.google.com/presentation/d/1lBmtptTsUn0ePogB_aNFTJAf1sfB-aJxriyNbe7ORTM/edit?slide=id.p1#slide=id.p1)
## Introduction
In this exercise, your task is that given any path (relative or absolute), produce its canonical absolute form — **without touching the real filesystem**, purely by string manipulation.

## Rules
| Component | Meaning | Action |
|-----------|---------|--------|
| `/` at start | Absolute path | Start from root `"/"` |
| (no `/` at start) | Relative path | Start from `curr_working_dir` |
| `.` | Current directory | Skip it |
| `..` | Parent directory | Remove the last segment |
| anything else | Directory/file name | Append it |

## TODO
Implement the my_realpath function to resolve relative and absolute file paths  
```c
char* my_realpath(const char* path, char* resolved_path)
```

## Expected Result
```
[0] "." --> "/path/to/current/directory"
[1] ".." --> "/path/to/current"
[2] "./test" --> "/path/to/current/directory/test"
[3] "../parent" --> "/path/to/current/parent"
[4] "dir1/dir2/../../dir3" --> "/path/to/current/directory/dir3"
[5] "/absolute/path" --> "/absolute/path"
[6] "relative/./path" --> "/path/to/current/directory/relative/path"
```
