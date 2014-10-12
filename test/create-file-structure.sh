#!/bin/sh

rm -rf raid1 raid2
mkdir raid1 raid2

for raid in *; do
	mkdir $raid/public $raid/private $raid/backup $raid/anonymous
done

for i in $(seq 1 10); do
for RAND in $RANDOM; do
	for folder in */*; do
		mkdir $folder/$RAND;
		mkdir $folder/$RAND/foobar/
		echo "baz" > $folder/$RAND/foobar/baz
	done
done
done
