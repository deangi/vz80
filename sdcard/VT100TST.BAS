10 REM VT-100 escape sequence test for vZ80 console
20 E$ = CHR$(27)
25 WIDTH 255                              : REM disable MBASIC auto-wrap
30 PRINT E$;"[2J";E$;"[H";                : REM ED2 + home
40 PRINT "VT-100 TEST";
50 PRINT E$;"[3;10HCUP -> row 3 col 10";
60 PRINT E$;"[5;5H+----+";
70 PRINT E$;"[6;5H|    |";
80 PRINT E$;"[7;5H+----+";
90 PRINT E$;"[10;1Hsave->";E$;"7";        : REM DECSC after "save->"
100 PRINT E$;"[20;40H[detour at 20,40]";
110 PRINT E$;"8<-restored";                : REM DECRC, then text from saved pos
120 PRINT E$;"[12;1HXXXXXXXXXXXXXXXXXXXXXXXXXX cut right of col 14";
130 PRINT E$;"[12;15H";E$;"[K";            : REM EL0 from cursor to EOL
140 PRINT E$;"[14;1H1234567890123456789012345";
150 PRINT E$;"[14;15H";E$;"[1K";           : REM EL1 from BOL to cursor
160 PRINT E$;"[16;1Hjunk row 16";
170 PRINT E$;"[17;1Hjunk row 17";
180 PRINT E$;"[18;1Hjunk row 18";
190 PRINT E$;"[16;6H";E$;"[J";             : REM ED0 from cursor to EOS
200 PRINT E$;"[22;1Hrelative: ";
210 PRINT "ABC";E$;"[2DXX";                : REM CUB 2, overwrites BC with XX
220 PRINT E$;"[24;1Hdone.";
