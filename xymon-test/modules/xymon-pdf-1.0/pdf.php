<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0 Transitional//EN">
<HTML>
<HEAD>
<TITLE>Xymon : PDF reports</TITLE>

<!-- Styles for the menu bar -->
<link rel="stylesheet" type="text/css" href="/hobbit/menu/menu.css">
<meta http-equiv="refresh" content="600">

</HEAD>

<BODY BGCOLOR="red" BACKGROUND="/hobbit/gifs/bkg-blue.gif" TEXT="#D8D8BF" LINK="#00FFAA" VLINK="#FFFF44">

<TABLE SUMMARY="Topline" WIDTH="100%">
<TR><TD HEIGHT=16>&nbsp;</TD></TR>  <!-- For the menu bar -->
<TR>
  <TD VALIGN=MIDDLE ALIGN=LEFT WIDTH="30%">
    <FONT FACE="Arial, Helvetica" SIZE="+1" COLOR="silver"><B>Xymon</B></FONT>
  </TD>
  <TD VALIGN=MIDDLE ALIGN=CENTER WIDTH="40%">
    <CENTER><FONT FACE="Arial, Helvetica" SIZE="+1" COLOR="silver"><B>PDF REPORTS</B></FONT></CENTER>
  </TD>
  <TD VALIGN=MIDDLE ALIGN=RIGHT WIDTH="30%">
   <FONT FACE="Arial, Helvetica" SIZE="+1" COLOR="silver">
    <B>
	<? 
	//setlocale(LC_ALL, "fr_FR");
	echo htmlentities(strftime("%A %d %B %T")) ; 
	?>
    </B>
   </FONT>
  </TD>
</TR>
<TR>
  <TD COLSPAN=3> <HR WIDTH="100%"> </TD>
</TR>
</TABLE>

<BR><BR>

<center>
<form name="selprj" action="report.php" method="post">

<FONT FACE="Arial, Helvetica" SIZE="+1"><b>Hostname :</b></font>
<INPUT type="text" name="nom">

<BR><BR><BR>

<FONT FACE='Arial, Helvetica' SIZE='+1'><b>Select a period of time :</b></font>
<BR><BR>

<LABEL for='daily'><INPUT type='radio' name='chxrap' id='daily' value=daily>Daily</LABEL>
<BR>
<LABEL for='weekly'><INPUT type='radio' name='chxrap' id='weekly' value=weekly>Weekly</LABEL>
<BR>
<LABEL for='biweek'><INPUT type='radio' name='chxrap' id='biweek' value=biweek>Bi-weekly</LABEL>
<BR>
<LABEL for='monthly'><INPUT type='radio' name='chxrap' id='monthly' value=monthly checked>Monthly</LABEL>
<BR>
<LABEL for='annual'><INPUT type='radio' name='chxrap' id='annual' value=annual>Annually</LABEL>

<BR><BR><BR><BR>

<input type="submit" value="Report !">
</form> 

<!--
<b>Pour avoir plus d'informations sur les m&eacute;triques utilis&eacute;es et la m&eacute;trologie en g&eacute;n&eacute;ral, vous pouvez t&eacute;l&eacute;charger librement ce glossaire, au format pdf.</b>
<BR><BR>
<form method="post" action="report.php">
	<input type="hidden" name="nom" value="vide">
	<input type="hidden" name="chxrap" value="glossaire">
	<input type="submit" value="Glossary">
</form> 
-->

</center>

<BR><BR>

<TABLE SUMMARY="Bottomline" WIDTH="100%">
<TR>
  <TD> <HR WIDTH="100%"> </TD>
</TR>
<TR>
  <TD ALIGN=RIGHT><FONT FACE="Arial, Helvetica" SIZE="-2" COLOR="silver"><B><A HREF="http://hobbitmon.sourceforge.net/" style="text-decoration: none">Hobbit Monitor 4.2.0</A></B></FONT></TD>
</TR>
</TABLE>


<!-- menu script itself. you should not modify this file -->
<script type="text/javascript" language="JavaScript" src="/hobbit/menu/menu.js"></script>
<!-- items structure. menu hierarchy and links are stored there -->
<script type="text/javascript" language="JavaScript" src="/hobbit/menu/menu_items.js"></script>
<!-- files with geometry and styles structures -->
<script type="text/javascript" language="JavaScript" src="/hobbit/menu/menu_tpl.js"></script>
<script type="text/javascript" language="JavaScript">
        new menu (MENU_ITEMS, MENU_POS);
</script>

</BODY>
</HTML>
