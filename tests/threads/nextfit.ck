# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(nextfit) begin
(nextfit) Allocated A at index 0
(nextfit) Allocated B at index 4
(nextfit) Freed A
(nextfit) Allocated C at index 8
(nextfit) end
EOF
pass;
