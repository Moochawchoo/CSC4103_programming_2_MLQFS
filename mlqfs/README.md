#  MLQFS
CSC 4103 Operating Systems 
Programming Assignment #2
LSU FALL 2019

**Pierre Gabory**

## Compile
- Language: C 
`
$ gcc -o mlqfs -Iprioque/ prioque/prioque.c mlqfs.c
`

## Run

- `$ ./mlqfs [inputfile] [outputfile]`
- `$ ./mlqfs [inputfile]`, outputs in stdout
- `$ ./mlqfs`, uses standard io.

Tested examples:
`$ ./mlqfs < processes.txt`
`$ cat processes.txt | ./mlqfs`
`$ ./mlqfs processes.txt out.txt`
