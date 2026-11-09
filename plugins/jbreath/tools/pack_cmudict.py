# Packs the CMU Pronouncing Dictionary into src/cmudict_data.h for the Alien's typed words:
#    python3 tools/pack_cmudict.py cmudict.dict src/cmudict_data.h
# (cmudict.dict from github.com/cmusphinx/cmudict.)
import re,sys
V="AA AE AH AO AW AY EH ER EY IH IY OW OY UH UW".split()
C="B CH D DH F G HH JH K L M N NG P R S SH T TH V W Y Z ZH".split()
names=[v+str(s) for v in V for s in range(3)]+C
alpha=''.join(chr(c) for c in range(0x21,0x7f) if chr(c) not in '"\\')
assert len(alpha)>=len(names)
codes={n:alpha[i] for i,n in enumerate(names)}
ents={}
for line in open(sys.argv[1],encoding='utf8'):
    line=line.split('#')[0].strip()
    if not line: continue
    w,*ph=line.split()
    if '(' in w or not re.fullmatch(r"[a-z']+",w): continue
    ents[w]=''.join(codes[p] for p in ph)
lines=[];prev=''
for i,w in enumerate(sorted(ents)):
    n=0
    if i%64:
        while n<len(prev) and n<len(w) and n<40 and prev[n]==w[n]: n+=1
    lines.append(chr(48+n)+w[n:]+' '+ents[w]); prev=w
pieces=[];cur=''
for l in lines:
    if len(cur)+len(l)+1>12000: pieces.append(cur);cur=''
    cur+=l+'\n'
pieces.append(cur)
with open(sys.argv[2],'w') as f:
    f.write('''#pragma once
// Generated from the CMU Pronouncing Dictionary (cmudict, github.com/cmusphinx/cmudict) by
// tools/pack_cmudict.py: don't edit by hand. Each word's first pronunciation, sorted, one a line:
// how many letters it shares with the word before (a character from '0'), the rest of its letters,
// a space, and its phones, a character each (see kCmuPhones in lyrics.h). Every 64th word shares
// none, so a search can start there.
//
// Copyright (C) 1993-2015 Carnegie Mellon University. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer. The contents of this file are deemed to be source code.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
// This work was supported in part by funding from the Defense Advanced Research Projects Agency,
// the Office of Naval Research and the National Science Foundation of the United States of America,
// and by member companies of the Carnegie Mellon Sphinx Speech Consortium. We acknowledge the
// contributions of many volunteers to the expansion and improvement of this dictionary.
//
// THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND ANY EXPRESSED OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY NOR ITS
// EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
// WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

namespace jbreath::speech {

''')
    f.write('inline constexpr const char *kCmuPhoneAlphabet = R"cmu(%s)cmu";\n'%alpha[:len(names)])
    f.write('inline constexpr const char *kCmuPhoneNames[] = {%s};\n\n'%', '.join('"%s"'%n for n in names))
    f.write('inline constexpr const char *kCmuDict[] = {\n')
    for p in pieces: f.write('R"cmu(%s)cmu",\n'%p)
    f.write('};\ninline constexpr int kCmuDictPieces = %d, kCmuDictWords = %d;\n\n} // namespace jbreath::speech\n'%(len(pieces),len(ents)))
print(len(pieces), "pieces,", len(ents), "words")
