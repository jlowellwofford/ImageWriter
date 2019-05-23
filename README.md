ImageWriter performs a block level copy from one file to another, much like `dd`.

ImageWriter gives a nicer status than `dd`, and uses a buffer and threads to help speed things along.

Usage: `ImageWriter [-s block_size -b num_block] <in_file> <out_file>`

 block_size : size (in bytes) of each block
 num_block  : the number of blocks in the buffer

total buffer size in bytes = block_size * num_block

To compile: `make`

Author: J. Lowell Wofford <lowell@rescompllc.com>
