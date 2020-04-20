.PHONY:	clean check docker

all: pt8exam

pt8exam: pt8exam.c
	$(CC) -Wall -Wextra -Werror=unused-result -Werror=uninitialized -Werror -O2 -o $@ $<

check: pt8exam
	# ./pt8exam -m tests/CHEKMO.BN | tee check.log
	# @if [ "$$(grep -ao 'Overlap in area' check.log | wc -l)" -ne "8" ]; then false; fi

# Compile the code inside a docker container using this Makefile again
docker:	
	docker run --rm -v "$(PWD)":/usr/src/myapp -w /usr/src/myapp gcc:latest make

clean:
	rm -f pt8exam *.log
