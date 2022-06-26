# pp

package builder for c

## Usage

```
pp [path_to_directory] [name_of_executebale]
```
This will create the directory `package` containing every source and header file in `[path_to_directory]` and an additional src file `compile.c` with the associated compile instructions

### Example
`pp . pp`

## Install
### Gnu/Linux

```
gcc compile.c -o comp && ./comp gcc
```

## Devel
Every devel version of this is on `pp_devel`, because  `main` is packaged using `pp`
