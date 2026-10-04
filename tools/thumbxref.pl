#!/usr/bin/perl
# thumbxref.pl: which functions of a 32-bit ARM .so call a given function.
# Scans the Thumb-2 code for BL / BLX / B.W whose target is the function
# itself or its PLT entry (calls between the library's own exported functions
# go through the PLT), and names the function each call sits in.
#   perl thumbxref.pl <lib.so> <symbol regex>
use strict;
use warnings;

my ($path, $pat) = @ARGV;
die "usage: thumbxref.pl <lib.so> <symbol regex>\n" unless $path && $pat;

open(my $fh, '<:raw', $path) or die "$path: $!\n";
my $d = do { local $/; <$fh> };
close($fh);

my ($phoff, $phentsize, $phnum) = (unpack('V', substr($d, 28, 4)), unpack('v v', substr($d, 42, 4)));
my (@loads, $dyn_off, $dyn_size);
for my $i (0 .. $phnum - 1) {
  my ($t, $off, $va, $pa, $fsz, $msz, $flags) = unpack('V7', substr($d, $phoff + $i * $phentsize, 28));
  push @loads, [$va, $off, $fsz, $flags] if $t == 1;
  ($dyn_off, $dyn_size) = ($off, $fsz) if $t == 2;
}
sub off {
  my ($va) = @_;
  for my $l (@loads) { return $va - $l->[0] + $l->[1] if $va >= $l->[0] && $va < $l->[0] + $l->[2]; }
  return undef;
}
sub u32 { my $o = off($_[0]); return defined $o ? unpack('V', substr($d, $o, 4)) : 0; }

my %dyn;
for (my $o = $dyn_off; $o < $dyn_off + $dyn_size; $o += 8) {
  my ($tag, $val) = unpack('V V', substr($d, $o, 8));
  last if $tag == 0;
  $dyn{$tag} = $val unless $tag == 1;
}
my ($strtab, $symtab) = (off($dyn{5}), off($dyn{6}));
# The symbol count: DT_HASH's nchain, or (a library with DT_GNU_HASH only)
# the .dynsym section's size.
my $nsyms = 0;
if (defined $dyn{4}) {
  $nsyms = (unpack('V V', substr($d, off($dyn{4}), 8)))[1];
} else {
  my ($shoff, $shentsize, $shnum) = (unpack('V', substr($d, 32, 4)), unpack('v v', substr($d, 46, 4)));
  for my $i (0 .. $shnum - 1) {
    my ($sname, $stype, $sflags, $saddr, $soff, $ssize) = unpack('V6', substr($d, $shoff + $i * $shentsize, 24));
    $nsyms = $ssize / 16 if $stype == 11;
  }
}
die "no symbol count (neither DT_HASH nor .dynsym)
" unless $nsyms;
sub symname { my ($i) = @_; return unpack('Z*', substr($d, $strtab + unpack('V', substr($d, $symtab + $i * 16, 4)), 300)); }

# Functions sorted by address (to name the caller), and the wanted targets.
my (@funcs, %want);
for my $i (1 .. $nsyms - 1) {
  my ($name, $value, $size, $info, $other, $shndx) = unpack('V V V C C v', substr($d, $symtab + $i * 16, 16));
  next unless $shndx && $value && ($info & 0xf) == 2;
  my $n = symname($i);
  push @funcs, [$value & ~1, $size, $n];
  $want{$value & ~1} = $n if $n =~ /$pat/;
}
@funcs = sort { $a->[0] <=> $b->[0] } @funcs;

# The wanted functions' PLT entries: add ip, pc, #a; add ip, ip, #b; ldr pc, [ip, #c]!
sub ror { my ($v, $r) = @_; $r &= 31; return $r ? (($v >> $r) | ($v << (32 - $r))) & 0xffffffff : $v; }
my %slot_name;
if (defined $dyn{23}) {
  my ($rel, $relsz) = (off($dyn{23}), $dyn{2});
  for (my $o = 0; $o < $relsz; $o += 8) {
    my ($r_off, $r_info) = unpack('V V', substr($d, $rel + $o, 8));
    my $n = symname($r_info >> 8);
    $slot_name{$r_off} = $n if $n =~ /$pat/;
  }
}
# An import has no address of its own: it is found by its PLT entry alone.
die "no function matches $pat
" unless %want || %slot_name;
my ($text) = grep { $_->[3] & 1 } @loads; # the executable segment
my ($tva, $toff, $tsz) = @$text;
my %plt;
for (my $o = 0; $o + 12 <= $tsz; $o += 4) {
  my @w = unpack('V3', substr($d, $toff + $o, 12));
  next unless ($w[0] & 0xfffff000) == 0xe28fc000 && ($w[1] & 0xfffff000) == 0xe28cc000 && ($w[2] & 0xfffff000) == 0xe5bcf000;
  my $va = $tva + $o;
  my $slot = ($va + 8 + ror($w[0] & 0xff, 2 * (($w[0] >> 8) & 0xf)) + ror($w[1] & 0xff, 2 * (($w[1] >> 8) & 0xf)) + ($w[2] & 0xfff)) & 0xffffffff;
  $plt{$va} = $slot_name{$slot} if exists $slot_name{$slot};
}

sub enclosing {
  my ($va) = @_;
  my ($lo, $hi) = (0, $#funcs);
  while ($lo < $hi) {
    my $mid = int(($lo + $hi + 1) / 2);
    if ($funcs[$mid][0] <= $va) { $lo = $mid; } else { $hi = $mid - 1; }
  }
  my $f = $funcs[$lo];
  return $va >= $f->[0] && $va < $f->[0] + ($f->[1] || 4) ? sprintf('%s+0x%x', $f->[2], $va - $f->[0]) : sprintf('0x%x', $va);
}

# Every halfword position: a BL/BLX/B.W there whose target is wanted.
my %seen;
for (my $o = 0; $o + 4 <= $tsz; $o += 2) {
  my ($h, $l) = unpack('v v', substr($d, $toff + $o, 4));
  next unless ($h & 0xf800) == 0xf000 && ($l & 0x8000) && ($l & 0x5000);
  my $s = ($h >> 10) & 1;
  my ($j1, $j2) = (($l >> 13) & 1, ($l >> 11) & 1);
  my $imm = ($s << 24) | ((($j1 ^ $s) ^ 1) << 23) | ((($j2 ^ $s) ^ 1) << 22) | (($h & 0x3ff) << 12) | (($l & 0x7ff) << 1);
  $imm -= 1 << 25 if $s;
  my $pc = $tva + $o;
  my $blx = ($l & 0xd000) == 0xc000;
  my $t = (($blx ? (($pc + 4) & ~3) : $pc + 4) + $imm) & 0xffffffff;
  my $name = $blx ? $plt{$t} : $want{$t & ~1};
  next unless defined $name;
  my $c = enclosing($pc);
  print "$name  <-  $c\n" unless $seen{"$name $c"}++;
}
