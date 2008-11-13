<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0 Transitional//EN">
<HTML>
<HEAD>
<TITLE>Xymon : PDF analysis</TITLE>

<!-- Styles for the menu bar -->
<link rel="stylesheet" type="text/css" href="/hobbit/menu/menu.css">
<meta http-equiv="refresh" content="600">

<SCRIPT language="JavaScript">
<!--   // if your browser doesn't support Javascript, following lines will be hidden.

var loadavg_good_text="Enter a text to describe CPU load."
var memory_good_text="Enter a text to describe Memory Utilization."
var network_good_text="Enter a text to describe Network Traffic."
var runqueue_good_text="Enter a text to describe Run-queue Length."
var iowait_good_text="Enter a text to describe I/O wait Rate." 
var vmstat_good_text="Enter a text to describe CPU Utilization."
var conclusion_good_text="What is your conclusion ?"

var loadavg_bad_text="Enter a text to describe CPU load."
var memory_bad_text="Enter a text to describe Memory Utilization."
var network_bad_text="Enter a text to describe Network Traffic."
var runqueue_bad_text="Enter a text to describe Run-queue Length."
var iowait_bad_text="Enter a text to describe I/O wait Rate."
var vmstat_bad_text="Enter a text to describe CPU Utilization."
var conclusion_bad_text="What is your conclusion ?"

var loadavg_ugly_text="Enter a text to describe CPU load."
var memory_ugly_text="Enter a text to describe Memory Utilization."
var network_ugly_text="Enter a text to describe Network Traffic."
var runqueue_ugly_text="Enter a text to describe Run-queue Length."
var iowait_ugly_text="Enter a text to describe I/O wait Rate."
var vmstat_ugly_text="Enter a text to describe CPU Utilization."
var conclusion_ugly_text="What is your conclusion ?"

/*var loadavg_good_text="La charge moyenne est comprise entre 0.5 et 1. Le serveur est donc sollicité régulièrement mais pas en permanence. Eventuellement, de nouvelles applications pourront être hébergées. Nous sommes donc dans une bonne configuration. Le serveur est adapté en terme de puissance CPU." ;
var memory_good_text="La mémoire physique est utilisée à 80% en moyenne et atteint les 100% en pic. C'est une configuration correcte. Idem pour l'utilisation de la swap : son utilisation ne dépasse pas la taille de la mémoire physique, il y a donc suffisament de RAM dans le serveur. De nouvelles applications peuvent être installées." ;
var network_good_text="Pas de traffic important et pas d'utilisation intensive sur une longue période de temps. Configuration optimale." ;
var runqueue_good_text="La run-queue égale le nombre de processeurs présents dans le serveur (X). Le système n'a donc pas une charge de travail lourde." ;
var iowait_good_text="Le taux d'I/O wait, en moyenne, ne dépasse pas les 15%. Le serveur ne perd donc pas de temps en attente d'I/O. Il n'y a pas de problèmes sur les disques du système. " ;
var vmstat_good_text="Le temps dédié au système est inférieur à 15% donc le nombre d'appels systèmes ou de demandes d'I/O est correct. Le temps utilisateur (pour les applications) est inférieur à 90% ce qui signifie que le serveur peut accueillir d'autres applications. Le temps d'inactivité est bon puisque compris entre 40 et 90%. Le serveur passe donc une partie de son temps à attendre des demandes de traitements." ;
var conclusion_good_text="Après analyse de ces graphes, nous pouvons dire que le serveur est adapté pour les applications et les traitements qu'il héberge actuellement. Il pourra même accueillir d'autres applications si nécessaire. Il faudra faire attention à ne pas installer ou migrer des applications trop gourmandes sous peine de voir les performances s'effondrer. Le serveur est bien dimensionné." ;

var loadavg_bad_text="La charge moyenne est inférieure à 0.5 en moyenne. Quelques pics atteignent 1 démontrant que la machine est sollicitée par moment. Nous sommes donc dans une configuration correcte en terme de puissance puisque le serveur pourra gérer sans problèmes des pics de charge imprévus. Par contre, d'un point de vue financier, le rapport puissance/prix n'est pas bon du tout. Le serveur est largement sous-utilisé. En terme de puissance CPU, d'après ce graphe, nous sommes dans un cas de sur-dimensionnement. Afin de rentabiliser au mieux la machine, il serait envisageable d'y mettre de nouvelles applications." ;
var memory_bad_text="La mémoire physique est utilisée à 50% en moyenne. Une fois de plus, il s'agit d'une configuration correcte. Idem pour l'utilisation de la swap. L'espace réservé pour son utilisation n'est pas occupé ou exceptionnellement." ;
var network_bad_text="Pas de traffic important et pas d'utilisation intensive sur une longue période de temps. Configuration optimale." ;
var runqueue_bad_text="La run-queue est inférieure à 1 et ne dépasse que très peu fréquemment cette valeur. Comme nous l'avons déjà vu avec les autres graphes, le système n'a pas une charge de travail lourde. Elle est même quasiment nulle." ;
var iowait_bad_text="Pas ou peu de valeurs au-dessus de 0%. Le serveur ne perd donc pas de temps en attente d'I/O et répond de manière optimale." ;
var vmstat_bad_text="Le temps dédié au système est inférieur à 10% en moyenne donc le nombre d'appels systèmes ou de demandes d'I/O est correct voire inexistant. Le temps utilisateur (devoué aux applications) est inférieur à 10%, ce qui signifie que le serveur peut accueillir sans soucis d'autres applications. Le temps d'inactivité est maximal puisque compris entre 90 et 100%." ;
var conclusion_bad_text="Après analyse de ces graphes, nous pouvons dire que le serveur est clairement adapté en terme de puissance puisqu'il ne travaille quasiment jamais ! Un serveur coûtant une somme d'argent non-négligeable, il est recommandé de mettre les nouvelles applications sur cette machine en priorité. De même, pour des applications devant migrer, ce serveur pourra les accueillir sans problèmes. Le serveur est sur-dimensionné." ;

var loadavg_ugly_text="La charge moyenne est très supérieure à 1. Nous sommes donc dans une position délicate : le serveur est inadapté en terme de puissance CPU. Ce qui signifie qu'il faudra certainement rajouter des processeurs ou pire, migrer certaines applications vers d'autres serveurs moins chargés. Les cinq autres graphes vont nous aider à confirmer cette tendance." ;
var memory_ugly_text="La mémoire physique est utilisée à 100% en moyenne. L'espace de swap est utilisé à plus de 50% et dépasse la taille totale de mémoire physique présente dans le système. Si la swap atteint également les 100%, le système d'exploitation refusera l'exécution de nouvelles applications et pourra, dans le pire des cas, provoquer un redémarrage inopiné du système. Des applications réagissant de manière incohérente ou imprévue sont également à prévoir dans une telle situation." ;
var network_ugly_text="Traffic important et utilisation intensive sur une longue période de temps. Vérifier qu'aucune application ne s'est plantée et ne provoque de charge réseau importante." ;
var runqueue_ugly_text="La run-queue est supérieure au nombre de processeurs (si Solaris supérieure à trois fois le nombre de CPUs) présents dans la machine (X). Le système a donc une charge de travail trop lourde." ;
var iowait_ugly_text="Pics fréquents au-dessus de 30% et la moyenne est comprise entre 15 et 30%. Le serveur perd donc un temps considérable en attente d'I/O. Ceci est probablement lié au phénomène de swap qui provoque une utilisation intensive des disques. En conséquence, le système est beaucoup plus lent à réagir." ;
var vmstat_ugly_text="Le temps dédié au système est supérieur à 10%, cela confirme que le nombre d'appels systèmes ou de demandes d'I/O est très élevé. Le temps utilisateur (pour les applications) atteint régulièrement ou continuellement 100%. Le serveur ne peut plus accueillir d'autres applications. Le temps d'inactivité est exécrable puisque compris entre 0 et 10%. Le système n'arrive pas à dégager suffisament de temps pour traiter toutes les applications." ;
var conclusion_ugly_text="Après analyse de ces graphes, nous pouvons dire que le serveur n'a pas la puissance requise pour la charge de travail impartie. L'urgence de la situation est réelle et il est clairement le temps de réfléchir à un plan de migration de certaines applications vers d'autres serveurs moins chargés ou de réaliser un upgrade matériel, comme un ajout de RAM et/ou de CPUs. Dans tous les cas, ce serveur est désormais dangereux pour les applications qu'il héberge car les temps de réponse, de traitement et de disponibilité ne sont tout simplement plus garantis. Le serveur est sous-dimensionné." ;
*/

//-->  // End of hidden part
</SCRIPT>

</HEAD>

<BODY BGCOLOR="red" BACKGROUND="/hobbit/gifs/bkg-blue.gif" TEXT="#D8D8BF" LINK="#00FFAA" VLINK="#FFFF44">

<TABLE SUMMARY="Topline" WIDTH="100%">
<TR><TD HEIGHT=16>&nbsp;</TD></TR>  <!-- For the menu bar -->
<TR>
  <TD VALIGN=MIDDLE ALIGN=LEFT WIDTH="30%">
    <FONT FACE="Arial, Helvetica" SIZE="+1" COLOR="silver"><B>Xymon</B></FONT>
  </TD>
  <TD VALIGN=MIDDLE ALIGN=CENTER WIDTH="40%">
    <CENTER><FONT FACE="Arial, Helvetica" SIZE="+1" COLOR="silver"><B>PDF Analysis</B></FONT></CENTER>
  </TD>
  <TD VALIGN=MIDDLE ALIGN=RIGHT WIDTH="30%">
   <FONT FACE="Arial, Helvetica" SIZE="+1" COLOR="silver">
     <B>
	<? 
	// setlocale(LC_ALL, "fr_FR");
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

<BR>


<center>

<form name="selprj" action="report.php" method="post">

Hostname :
<INPUT type="text" name="nom">

<BR><BR>

<LABEL for='loaded'><INPUT type="radio" name="verif" id='loaded' value=ok  onClick=" this.form.loadavg.value=loadavg_good_text ; this.form.memory.value=memory_good_text ; this.form.runqueue.value=runqueue_good_text ; this.form.network.value=network_good_text ; this.form.iowait.value=iowait_good_text ; this.form.vmstat.value=vmstat_good_text ; this.form.conclusion.value=conclusion_good_text " checked>Loaded</LABEL>
<LABEL for='uloaded'><INPUT type="radio" name="verif" id='uloaded' value=mok  onClick=" this.form.loadavg.value=loadavg_bad_text ; this.form.memory.value=memory_bad_text ; this.form.runqueue.value=runqueue_bad_text ; this.form.network.value=network_bad_text ; this.form.iowait.value=iowait_bad_text ; this.form.vmstat.value=vmstat_bad_text ; this.form.conclusion.value=conclusion_bad_text ">Under-loaded</LABEL>
<LABEL for='hloaded'><INPUT type="radio" name="verif" id='hloaded' value=nok  onClick=" this.form.loadavg.value=loadavg_ugly_text ; this.form.memory.value=memory_ugly_text ; this.form.runqueue.value=runqueue_ugly_text ; this.form.network.value=network_ugly_text ; this.form.iowait.value=iowait_ugly_text ; this.form.vmstat.value=vmstat_ugly_text ; this.form.conclusion.value=conclusion_ugly_text ">Heavy-loaded</LABEL>

<BR><BR>

<textarea name="loadavg" COLS=50 ROWS=5 WRAP=physical>CPU Load</TEXTAREA>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
<textarea name="memory" COLS=50 ROWS=5 WRAP=physical>Memory</TEXTAREA>
<BR>
<textarea name="runqueue" COLS=50 ROWS=5 WRAP=physical>Run-queue</TEXTAREA>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
<textarea name="network" COLS=50 ROWS=5 WRAP=physical>Network Traffic</TEXTAREA>
<BR>
<textarea name="iowait" COLS=50 ROWS=5 WRAP=physical>I/O wait</TEXTAREA>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
<textarea name="vmstat" COLS=50 ROWS=5 WRAP=physical>CPU Utilization</TEXTAREA>
<BR><BR>
<textarea name="conclusion" COLS=50 ROWS=5 WRAP=physical>Conclusion</TEXTAREA>
<BR>
<BR>

<INPUT type="hidden" name="chxrap" value="analyse">
<input type="submit" name="analyse" value="Report !">
</form> 


</center>


<TABLE SUMMARY="Bottomline" WIDTH="100%">
<TR>
  <TD> <HR WIDTH="100%"> </TD>
</TR>
<TR>
  <TD ALIGN=RIGHT><FONT FACE="Arial, Helvetica" SIZE="-2" COLOR="silver"><B><A HREF="http://hobbitmon.sourceforge.net/" style="text-decoration: none">Xymon</A></B></FONT></TD>
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
