CC = /opt/homebrew/bin/g++-14
CFLAGS = -std=c++17 -Wall -Wextra -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lxlsxwriter

all: mips_pipeline

mips_pipeline: mips_pipeline.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

mips_pipeline.o: mips_pipeline.cpp
	$(CC) $(CFLAGS) -c $< -o $@

sim: mips_pipeline
	./mips_pipeline

clean:
	rm -f *.o mips_pipeline pipeline_report.xlsx

assembler:
	python3 assembler.py

push:
	git add .
	git commit -m "Update project"
	git push origin main
	
.PHONY: all clean sim
