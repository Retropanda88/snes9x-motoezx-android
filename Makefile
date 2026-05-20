CXX = g++

# Añadimos -fcommon al final para fusionar de forma segura las múltiples definiciones de DSP1
CXXFLAGS = -O2 -fpermissive -Wno-narrowing   -w -I./snes -fcommon

LIBS = snes/snes.a -lSDL

TARGET = snesemu

all:
	$(CXX) main.cpp snes_player.cpp $(LIBS) $(CXXFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)
