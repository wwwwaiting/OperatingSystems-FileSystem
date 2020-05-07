all: ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker ext2_rm_bonus ext2_restore_bonus

ext2_mkdir:  ext2_mkdir.c ext2_helper.c ext2.h ext2_helper.h
	gcc -Wall -g -o $@ $^ -lm

ext2_cp:  ext2_cp.c ext2_helper.c ext2.h ext2_helper.h
	gcc -Wall -g -o $@ $^ -lm

ext2_ln:  ext2_ln.c ext2_helper.c ext2.h ext2_helper.h
	gcc -Wall -g -o $@ $^ -lm

ext2_rm:  ext2_rm.c ext2_helper.c ext2.h ext2_helper.h
	gcc -Wall -g -o $@ $^ -lm

ext2_restore:  ext2_restore.c ext2_helper.c ext2.h ext2_helper.h
	gcc -Wall -g -o $@ $^ -lm

ext2_checker:  ext2_checker.c ext2_helper.c ext2.h ext2_helper.h
	gcc -Wall -g -o $@ $^ -lm

ext2_rm_bonus:  ext2_rm_bonus.c ext2_helper.c ext2.h ext2_helper.h
	gcc -Wall -g -o $@ $^ -lm

ext2_restore_bonus:  ext2_restore_bonus.c ext2_helper.c ext2.h ext2_helper.h
	gcc -Wall -g -o $@ $^ -lm

clean:
	rm -f *.o ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker ext2_rm_bonus ext2_restore_bonus