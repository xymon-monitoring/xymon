<?

//
// Query to get infos about server from a MySQL DB.
//

@mysql_connect($host,$user,$password) or erreur( $msgerror ) ;
$result_sql_idcard = mysql_db_query($db,$sql_idcard) ;

// Start of the ID card
$pdf->AddPage();
$pdf->SetFont('Times','',12) ;

$pdf->SetXY(25,40) ;
$pdf->Image($idcardimg,10,38,0,13) ;
$pdf->SetTitle($titreidcard) ;
$pdf->Titre($titreidcard) ;
$pdf->Ln(5) ;

while($row = mysql_fetch_array($result_sql_idcard))
{
    $pdf->SetFont('Times','',12);
    $pdf->SetXY(20,60);
    $pdf->Cell(23,5,$idcardhost,1,0,'L',1);
    $pdf->Cell(70,5,$row['host_real'],1,1,'L',1);
    $pdf->SetXY(20,65);
    $pdf->Cell(23,5,$idcardalias,1,0,'L');
    $pdf->Cell(70,5,$row['hostname'],1,1,'L');

    $pdf->SetXY(130,65);
    $pdf->Cell(20,5,$idcardgroup,1,0,'L');
    $pdf->Cell(40,5,$row['groupe'],1,1,'L');
    $pdf->SetXY(130,60);
    $pdf->Cell(20,5,$idcardproj,1,0,'L',1);
    $pdf->Cell(40,5,$row['projet'],1,1,'L',1);

    $pdf->Ln(5);

    $pdf->SetFillColor(255);
    $pdf->RoundedRect(50, 80, 110, 12, 5, 'DF', '1234');
    $pdf->SetXY(80,84);
    $pdf->Cell(25,5,$idcardloc,0,0,'L');
    $pdf->Cell(50,5,$row['localisation'],0,1,'L');

    $pdf->Ln(5);

    $pdf->SetFillColor(255,250,205);
    $pdf->RoundedRect(45, 97, 118, 55, 5, 'DF', '12');
    $pdf->SetFillColor(233,150,122);
    $pdf->SetXY(50,105);
    $pdf->Cell(40,5,$idcardnbcpu,0,0,'L',1);
    $pdf->Cell(68,5,$row['nb_cpu'],0,1,'L',1);
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardfamily,0,0,'L');
    $pdf->Cell(68,5,$row['type_cpu'],0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardfreq,0,0,'L');
    $pdf->Cell(68,5,$row['freq_cpu'].' MHz',0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,'',0,0,'L');
    $pdf->Cell(68,5,'',0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardram,0,0,'L',1);
    $pdf->Cell(68,5,$row['ram'].' Mo',0,1,'L',1);
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardswap,0,0,'L');
    $pdf->Cell(68,5,$row['swap_taille'].' Mo',0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,'',0,0,'L');
    $pdf->Cell(68,5,'',0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardos,0,0,'L',1);
    $pdf->Cell(68,5,$row['version_os'],0,1,'L',1);
    $pdf->Ln(5);

    $pdf->SetFillColor(255,250,205);
    $pdf->RoundedRect(45, 165, 118, 45, 5, 'DF', '');
    $pdf->SetFillColor(233,150,122);
    $pdf->SetXY(50,170);
    $pdf->Cell(40,5,$idcardfonc,0,0,'L',1);
    $pdf->Cell(68,5,$row['fonction'],0,1,'L',1);
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardda,0,0,'L');
    $pdf->Cell(68,5,strftime("%d %B %Y",strtotime($row['dateachat'])),0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,'',0,0,'L');
    $pdf->Cell(68,5,'',0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardconst,0,0,'L');
    $pdf->Cell(68,5,$row['constructeur'],0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardmodel,0,0,'L',1);
    $pdf->Cell(68,5,$row['model'],0,1,'L',1);
    $pdf->SetX(50);
    $pdf->Cell(40,5,'',0,0,'L');
    $pdf->Cell(68,5,'',0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardvirtual,0,0,'L');
if ($row['virtual'] == "n")
{
$virtual = "Non" ;
} else {
$virtual = "Oui" ;
}
    $pdf->Cell(68,5,$virtual,0,1,'L');

    $pdf->Ln(5);

    $pdf->SetFillColor(255,250,205);
    $pdf->RoundedRect(45, 222, 118, 35, 5, 'DF', '34');
    $pdf->SetFillColor(233,150,122);
    $pdf->SetXY(50,233);
    $pdf->Cell(40,5,$idcardsan,0,0,'L');
if ($row['san_ok'] == "0")
{
$sanok = $idcardno ;
} else {
$sanok = $idcardyes ;
}
    $pdf->Cell(68,5,$sanok,0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardnbhba,0,0,'L');
    $pdf->Cell(68,5,$row['nb_hba'],0,1,'L');
    $pdf->SetX(50);
    $pdf->Cell(40,5,$idcardmodelhba,0,0,'L',1);
    $pdf->Cell(68,5,$row['type_hba'],0,1,'L',1);

    $pdf->Ln(35);

} // End of while

?>
