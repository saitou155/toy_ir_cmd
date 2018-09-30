CC      = cc
OBJS    = toy_ir_cmd.o

toy_ir_cmd: $(OBJS)
	$(CC) -Wall -g -o $@ $(OBJS) -lusb-1.0 

.c.o:
	$(CC) -c -g $<

clean:
	@rm -f $(OBJS)
	@rm -f toy_ir_cmd
