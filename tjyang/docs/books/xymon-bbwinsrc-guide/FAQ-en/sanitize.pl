# $Id: sanitize.pl,v 1.11 2003/06/20 10:51:44 rf Exp rf $

# provides the sanitize_line function used by all texfaq2* files
#
# will not compile standing alone

# Convert a LaTeX line to HTML:

sub sanitize_line {
    s"\&"\&amp\;"g;
    s"\<"\&lt\;"g;
    s"\>"\&gt\;"g;
    if ($converting && !$ignoring) {
	s"\\vspace\*\{[^\}]*\}""g;
	s"``"\""g;
	s"''"\""g;
        s"^\s*$"<p>";
	s"\%.*"";
	s"\\obracesymbol\{\}"\&lbrace\;"g;
	s"\\cbracesymbol\{\}"\&rbrace\;"g;
        s"\\\{"\&lbrace\;"g;
	s"\\\}"\&rbrace\;"g;
	s"\\ae\{\}"\&aelig\;"g;
        s"\\AllTeX\{\}"(La)TeX"g;
	s"\\twee\{\}"2e"g;
        s"\\LaTeXe\{\}"LaTeX2e"g;
        s"\\LaTeXo\{\}"LaTeX 2.09"g;
        s"\\MF\{\}"Metafont"g;
        s"\\MP\{\}"MetaPost"g;
        s"\\BV\{\}"<i>Baskerville</i>"g;
        s"\\TUGboat\{\}"<i>TUGboat</i>"g;
        s"\\PDFTeX\{\}"PDFTeX"g;
        s"\\PDFLaTeX\{\}"PDFLaTeX"g;
        s"\\CONTeXT\{\}"ConTeXt"g;
        s"\\NTS\{\}"<i>NTS</i>"g;
        s"\\eTeX\{\}"e-TeX"g;
        s"\\Eplain\{\}"Eplain"g;
        s"\\TeXsis\{\}"TeXsis"g;
	s"\\YandY\{\}"Y&amp;Y"g;
        s"\\WYSIWYG\{\}"WYSIWYG"g;
	s"\\PS\{\}"PostScript"g;
        s"\\dots\{\}"..."g;
        s"\\ldots\{\}"..."g;
        s"\\large""g;
        s"\\pounds\{\}"&pound;"g;
	s"\\arrowhyph\{\}"-&gt; "g;
        s"\\protect""g;
        s"\-\-\-"\-"g;
        s"\-\-"\-"g;
        s"\\(\w+)\{\}"$1"g;
        s"\\\"a"\&auml\;"g;
        s"\\\"o"\&ouml\;"g;
        s"\\\'e"\&eacute\;"g;
        s"\\\^e"\&ecirc\;"g;
	s"\\\'o"\&oacute\;"g;
	s"\\ss"\&szlig\;"g;
       	s"`"'"g;
        s"\\label\{[^\}]*\}""g;
        s"\\acro\{([^\}]*)\}"$1"g;
        s"\\ensuremath\{([^\}]*)\}"$1"g;
        s"\\emph\{([^\}]*)\}"<em>$1</em>"g;
        s"\\textit\{([^\}]*)\}"<em>$1</em>"g;
        s"\\textsl\{([^\}]*)\}"<em>$1</em>"g;
        s"\\meta\{([^\}]*)\}"&lt\;<em>$1</em>&gt\;"g;
        s"\\texttt\{([^\}]*)\}"<code>$1</code>"g;
	s"\\textbf\{([^\}]*)\}"<b>$1</b>"g;
	s"\\csx\{([^\}]*)\}"<code>\\$1</code>"g;
        s"\\parens\{([^\}]*)\}"$1"g;
        s"\\oparen\{\}""g;
        s"\\cparen\{\}""g;
	s"\~"\\textasciitilde{}"g if s"\\href\{([^\}]*)\}\{([^\}]*)\}"<a href=\"$1\">$2</a>";
        s"\\Q\{([^\}]*)\}""g;
        s"\\checked\{([^\}]*)\}\{([^\}]*)\}""g;
        s"\\footnote\{([^\}]*)\}""g;
	s"\\thinspace\{\}" "g;
        s"\\section\{([^\}]*)\}""g;
        s"\\subsection\{([^\}]*)\}""g;
        s"\$\\pi\$"<i>pi</i>"g;
        s"\$([^\$]*)\$"<i>$1</i>"g;
        s"\\ISBN\{([^\}]*)\}"ISBN $1"g;
        s"\\ProgName\|([^\|]*)\|"<i>$1</i>"g;
        s"\\ProgName\{([^\}]*)\}"<i>$1</i>"g;
        s"\\FontName\|([^\|]*)\|"<i>$1</i>"g;
        s"\\FontName\{([^\}]*)\}"<i>$1</i>"g;
        s"\\Package\|([^\|]*)\|"<i>$1</i>"g;
        s"\\Package\{([^\}]*)\}"<i>$1</i>"g;
        s"\\Class\|([^\|]*)\|"<i>$1</i>"g;
        s"\\Class\{([^\}]*)\}"<i>$1</i>"g;
        s"\\Email\|([^\|]*)\|"<i>$1</i>"g;
        s"\\mailto\|([^\|]*)\|"<a href\=\"mailto:$1\"><i>$1</i></a>"g;
        s"\\File\|([^\|]*)\|"<i>$1</i>"g;
        s"\\Newsgroup\|([^\|]*)\|"<i>$1</i>"g;
        s"\~"\\textasciitilde{}"g if s"\\URL\{([^\}]*)\}"\<a href\=\"$1\"\>$1\<\/a\>"g;
        s"\\FTP\|([^\|]*)\|"\<a href\=\"ftp\:\/\/$1\/\"\>$1\<\/a\>"g;
        s"\\CTAN\{([^\|]*)\}"\<a href\=\"ftp\://$arch/$root/$1\/\"\>$1\<\/a\>"g;
        s"\\Qref\[([^\]]*)\]\{([^\}]*)\}\{([^\}]*)\}"<a href\=\"$qref{$3}\">$2</a>"g;
	s"\\Qref\{([^\}]*)\}\{([^\}]*)\}"<a href\=\"$qref{$2}\">$1</a>"g;
	s"\\cmdinvoke\{([^\}]*)\}\{([^\}]*)\}\{([^\}]*)\}\{([^\}]*)\}\{([^\}]*)\}"<code>\\$1\{$2\}\{$3\}\{$4\}\{$5\}</code>"g;
	s"\\cmdinvoke\{([^\}]*)\}\[([^\]]*)\]\{([^\}]*)\}"<code>\\$1\[$2\]\{$3\}</code>"g;
	s"\\cmdinvoke\{([^\}]*)\}\{([^\}]*)\}\[([^\]]*)\]"<code>\\$1\{$2\}\[$3\]</code>"g;
	s"\\cmdinvoke\{([^\}]*)\}\{([^\}]*)\}\{([^\}]*)\}"<code>\\$1\{$2\}\{$3\}</code>"g;
	s"\\cmdinvoke\{([^\}]*)\}\{([^\}]*)\}"<code>\\$1\{$2\}</code>"g;
	s"\\cmdinvoke\{([^\}]*)\}\[([^\]]*)\]"<code>\\$1\[$2\]</code>"g;
	s"\\cmdinvoke\*\{([^\}]*)\}\{([^\}]*)\}\{([^\}]*)\}"<code>\\$1\{</code><em>$2</em><code>\}\{</code><em>$3</em><code>\}</code>"g;
	s"\\environment\{([^\}]*)\}"<code>$1</code>"g;
	s"\\pkgoption\{([^\}]*)\}"<code>$1</code>"g;
        s"\\path\|([^\|]*)\|"<i>$1</i>"g;
        s"\\begin\{htmlversion\}.*\n""g;
        s"\\end\{htmlversion\}.*\n""g;
        s"\\begin\{quote\}"<blockquote>"g;
        s"\\end\{quote\}"</blockquote>"g;
        s"\\begin\{description\}"<dl>"g;
        s"\\end\{description\}"</dl>"g;
        s"\\begin\{booklist\}"<dl>"g;
        s"\\end\{booklist\}"</dl>"g;
        s"\\begin\{proglist\}"<dl>"g;
        s"\\end\{proglist\}"</dl>"g;
        s"\\begin\{itemize\}"<ul>"g;
        s"\\end\{itemize\}"</ul>"g;
        s"\\begin\{enumerate\}"<ol>"g;
        s"\\end\{enumerate\}"</ol>"g;
	s"\\item\s*\[\\normalfont\{\}([^\]]*)\]"<dt>$1<dd>"g;
        s"\\item\s*\[([^\]]*)\]"<dt>$itemset$1$enditemset<dd>"g;
        s"\\item"<li>"g;
        s"\\\\(\[[^\]]*\])?"<br>"g;
        s"\|([^\|]+)\|"<code>$1</code>"g;
        s"\\\_"\_"g;
        s"\\textpercent"\%"g; # can't have \% in source...
        s"\\\$"\$"g;
        s"\\\#"\#"g;
        s"\\ " "g;
        s"\\\&"\&"g;
        s"\\\@""g;
        s"\\\;" "g;
        s"\\\," "g;
        s"\~" "g;
	s"\\nobreakspace" "g;
	s"\\textasciitilde"\~"g;
        s"\\textbar"\|"g;
        s"\\cs\<code\>"<code>\\"g;
        s"\&lbrace\;"\{";
        s"\&rbrace\;"\}";
	s"\\symbol\{([^\}]*)\}"$SymbolChar{$1}"g;
	s"\{\}""g;
	s"\\keywords\{([^\}]*)\}"<!-- $1 -->"g;
        s"\\relax""g;

	s"\\hphantom\{[^\}]*\}""g;
        s"\\nothtml\{[^\}]*\}""g;
	s"\\latexhtml\{[^\}]*\}\{([^\}]*)\}"$1"g;
	s"\\htmlonly\{([^\}]*)\}"$1"g;
    }
    if ( s"\\begin\{ctanrefs\}"<dl>"g ) {
	$itemset = "<tt><i>";
        $enditemset = "</i></tt>";
    }
    if ( s"\\end\{ctanrefs\}"</dl>"g ) {
	$itemset = "";
	$enditemset = "";
    }

    while ( /\\CTANref\{([^\}]*)\}/ ) {
	my $repl=generate_CTAN_ref("$1");
	s/\\CTANref\{([^\}]*)\}/$repl/;
    }

    $converting = 0 if s"\\begin\{verbatim\}"<pre>"g;
    $converting = 1 if s"\\end\{verbatim\}"</pre>"g;
    $ignoring++ if s"\\htmlignore""g;
    $ignoring-- if s"\\endhtmlignore""g;
    $ignoring++ if s"\\begin\{comment\}""g;
    $ignoring-- if s"\\end\{comment\}""g;
    $ignoring++ if s"\\begin\{footnoteenv\}""g;
    $ignoring-- if s"\\end\{footnoteenv\}""g;
    $_ = "" if $ignoring;
}

sub generate_CTAN_ref {
    if ( $ctanref_plus{$1} > 0 ) {
	$ret = "\<a href\=\"$proto_1://$arch_root$ctanref{$1}";
	$ret .= "$fmt_1\"\>$ctanref{$1}\<\/a\>";
	$ret .= " (\<a href\=\"$proto_2://$arch_root$ctanref{$1}$fmt_2\"\>$fmt_2_name\<\/a\>";
	$ret .= ", \<a href\=\"$proto_3://$host_d/$this_root/$ctanref{$1}$fmt_3\"\>$fmt_3_name\<\/a\>)";
    } elsif ( $ctanref_plus{$1} = 0 ) {
	$ret = "\<a href\=\"$proto_d://$host_d/$this_root/$ctanref{$1}\"\>" .
	       "$ctanref{$1}\<\/a\>";
    } else {
	$ret = "\<a href\=\"$proto_f://$arch_root$ctanref{$1}\"\>" .
	       "$ctanref{$1}\<\/a\>";
    }

    $ret;
}

1;
