# df_unwrap: join the rows df split in two. A device name too long for its
# column is printed on a line of its own, with the numbers on the next one.
df_unwrap() {
	sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
}
