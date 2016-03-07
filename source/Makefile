
target_file=webclient.bin


all:
	gcc webclient.c run_opts.c html_parser.c -o $(target_file)
	cp $(target_file) ../release/

clean:
	rm $(target_file)