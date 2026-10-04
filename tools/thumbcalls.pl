#!/usr/bin/perl
# thumbcalls.pl: what a Thumb-2 function of a 32-bit ARM .so calls, with the
# constants around the calls. Not a disassembler: it names the targets of
# BL / BLX / B.W (the library's own symbols, and imports through the PLT),
# shows immediates (MOVS, MOVW/MOVT, CMP), literal-pool loads and conditional
# branches, and prints everything else as hex. Enough to read a function's
# shape without binutils.
#   perl thumbcalls.pl <lib.so> <symbol regex> [max bytes]
use strict;
use warnings;

my ($path, $pat, $max) = @ARGV;
die "usage: thumbcalls.pl <lib.so> <symbol regex> [max bytes]\n" unless $path && $pat;

open(my $fh, '<:raw', $path) or die "$path: $!\n";
my $d = do { local $/; <$fh> };
close($fh);

my ($phoff, $phentsize, $phnum) = (unpack('V', substr($d, 28, 4)), unpack('v v', substr($d, 42, 4)));
my (@loads, $dyn_off, $dyn_size);
for my $i (0 .. $phnum - 1) {
  my ($t, $off, $va, $pa, $fsz) = unpack('V5', substr($d, $phoff + $i * $phentsize, 20));
  push @loads, [$va, $off, $fsz] if $t == 1;
  ($dyn_off, $dyn_size) = ($off, $fsz) if $t == 2;
}
sub off {
  my ($va) = @_;
  for my $l (@loads) { return $va - $l->[0] + $l->[1] if $va >= $l->[0] && $va < $l->[0] + $l->[2]; }
  return undef;
}
sub u16 { my $o = off($_[0]); return defined $o ? unpack('v', substr($d, $o, 2)) : 0; }
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

# The library's own functions by address, and the imports by GOT slot.
my (%by_addr, @funcs);
for my $i (1 .. $nsyms - 1) {
  my ($name, $value, $size, $info, $other, $shndx) = unpack('V V V C C v', substr($d, $symtab + $i * 16, 16));
  next unless $shndx && $value;
  my $n = symname($i);
  $by_addr{$value & ~1} //= $n;
  push @funcs, [$n, $value, $size] if ($info & 0xf) == 2 && $n =~ /$pat/;
}
my %got;
if (defined $dyn{23}) {
  my ($rel, $relsz) = (off($dyn{23}), $dyn{2});
  for (my $o = 0; $o < $relsz; $o += 8) {
    my ($r_off, $r_info) = unpack('V V', substr($d, $rel + $o, 8));
    $got{$r_off} = symname($r_info >> 8);
  }
}

# A PLT entry: add ip, pc, #a; add ip, ip, #b; ldr pc, [ip, #c]!
sub ror { my ($v, $r) = @_; $r &= 31; return $r ? (($v >> $r) | ($v << (32 - $r))) & 0xffffffff : $v; }
sub plt_name {
  my ($va) = @_;
  my @w = map { u32($va + $_ * 4) } 0 .. 2;
  return undef unless ($w[0] & 0xfffff000) == 0xe28fc000 && ($w[1] & 0xfffff000) == 0xe28cc000 && ($w[2] & 0xfffff000) == 0xe5bcf000;
  my $slot = $va + 8 + ror($w[0] & 0xff, 2 * (($w[0] >> 8) & 0xf)) + ror($w[1] & 0xff, 2 * (($w[1] >> 8) & 0xf)) + ($w[2] & 0xfff);
  return $got{$slot & 0xffffffff};
}
sub target {
  my ($va) = @_;
  return $by_addr{$va & ~1} // plt_name($va & ~3) // sprintf('0x%x', $va);
}

# "@0xb6b7c:400" as the pattern: the bytes at an address (a function with no
# symbol), instead of the functions a name matches.
if ($pat =~ /^\@(0x[0-9a-fA-F]+):(\d+)$/) {
  @funcs = ([sprintf('sub_%x', hex($1)), hex($1) | 1, $2]);
}
my @cc = qw(eq ne cs cc mi pl vs vc hi ls ge lt gt le);
for my $f (sort { $a->[1] <=> $b->[1] } @funcs) {
  my ($name, $value, $size) = @$f;
  next unless $value & 1; # Thumb only
  my $start = $value & ~1;
  my $end = $start + (defined $max && $max < $size ? $max : $size);
  printf "%s  (0x%x, %d bytes)\n", $name, $start, $size;
  for (my $pc = $start; $pc < $end;) {
    my $h = u16($pc);
    if (($h & 0xe000) == 0xe000 && ($h & 0x1800)) {
      my $l = u16($pc + 2);
      my $note = '';
      if (($h & 0xf800) == 0xf000 && ($l & 0x8000)) {
        my $s = ($h >> 10) & 1;
        my ($j1, $j2) = (($l >> 13) & 1, ($l >> 11) & 1);
        if ($l & 0x5000) { # BL (1x1), BLX (1x0), B.W (0x1)
          my $imm = ($s << 24) | ((($j1 ^ $s) ^ 1) << 23) | ((($j2 ^ $s) ^ 1) << 22) | (($h & 0x3ff) << 12) | (($l & 0x7ff) << 1);
          $imm -= 1 << 25 if $s;
          my $kind = ($l & 0xd000) == 0xd000 ? 'bl' : ($l & 0xd000) == 0xc000 ? 'blx' : 'b.w';
          my $base = $kind eq 'blx' ? (($pc + 4) & ~3) : $pc + 4;
          $note = "$kind " . target($base + $imm);
        }
      } elsif (($h & 0xfbf0) == 0xf240 || ($h & 0xfbf0) == 0xf2c0) {
        my $imm = (($h & 0xf) << 12) | ((($h >> 10) & 1) << 11) | ((($l >> 12) & 7) << 8) | ($l & 0xff);
        $note = sprintf('%s r%d, #%d (0x%x)', ($h & 0xfbf0) == 0xf240 ? 'movw' : 'movt', ($l >> 8) & 0xf, $imm, $imm);
      }
      printf "  %6x: %04x %04x  %s\n", $pc, $h, $l, $note;
      $pc += 4;
      next;
    }
    my $note = '';
    if (($h & 0xf800) == 0x2000) { $note = sprintf('movs r%d, #%d', ($h >> 8) & 7, $h & 0xff); }
    elsif (($h & 0xf800) == 0x2800) { $note = sprintf('cmp r%d, #%d', ($h >> 8) & 7, $h & 0xff); }
    elsif (($h & 0xf800) == 0x4800) {
      my $lit = (($pc + 4) & ~3) + ($h & 0xff) * 4;
      $note = sprintf('ldr r%d, =0x%x', ($h >> 8) & 7, u32($lit));
    }
    elsif (($h & 0xf000) == 0xd000 && (($h >> 8) & 0xf) < 14) {
      my $o = $h & 0xff; $o -= 256 if $o > 127;
      $note = sprintf('b%s %x', $cc[($h >> 8) & 0xf], $pc + 4 + $o * 2);
    }
    elsif (($h & 0xf800) == 0xe000) {
      my $o = $h & 0x7ff; $o -= 2048 if $o > 1023;
      $note = sprintf('b %x', $pc + 4 + $o * 2);
    }
    elsif (($h & 0xf500) == 0xb100) {
      $note = sprintf('cb%sz r%d, %x', ($h & 0x800) ? 'n' : '', $h & 7, $pc + 4 + (((($h >> 9) & 1) << 5) | (($h >> 3) & 0x1f)) * 2);
    }
    elsif (($h & 0xff87) == 0x4780) { $note = sprintf('blx r%d', ($h >> 3) & 0xf); }
    elsif (($h & 0xff00) == 0xbd00) { $note = 'pop {..., pc}'; }
    elsif ($h == 0x4770) { $note = 'bx lr'; }
    printf "  %6x: %04x       %s\n", $pc, $h, $note;
    $pc += 2;
  }
}
