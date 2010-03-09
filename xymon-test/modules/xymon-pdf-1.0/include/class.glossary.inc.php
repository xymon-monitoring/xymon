<?

class GLOSSAIRE extends TCPDF
{

//En-tete
public function Header()
{
    //Logo
    $this->Image('images/logobk.png',10,8,33);
    //Saut de ligne
    $this->Ln(10);
    //Police Times gras 15
    $this->SetFont('Times','B',15);
    //Decalage a droite
    $this->Cell(80);
    //Titre
    $this->Cell(0,20,'Glossaire de métrologie',0,0,'R');
    $this->Line(20,35,190,35);
    //Saut de ligne
    $this->Ln(20);
}

    var $B=0;
    var $I=0;
    var $U=0;
    var $HREF='';
    var $ALIGN='';

public function WriteHTML($html)
{
        //Parseur HTML
        $html=str_replace("\n",' ',$html);
        $a=preg_split('/<(.*)>/U',$html,-1,PREG_SPLIT_DELIM_CAPTURE);
        foreach($a as $i=>$e)
        {
            if($i%2==0)
            {
                //Texte
                if($this->HREF)
                    $this->PutLink($this->HREF,$e);
                elseif($this->ALIGN == 'center')
                    $this->Cell(0,5,$e,0,1,'C');
                else
                    $this->Write(5,$e);
            }
            else
            {
                //Balise
                if($e{0}=='/')
                    $this->CloseTag(strtoupper(substr($e,1)));
                else
                {
                    //Extraction des attributs
                    $a2=split(' ',$e);
                    $tag=strtoupper(array_shift($a2));
                    $prop=array();
                    foreach($a2 as $v)
                        if(ereg('^([^=]*)=["\']?([^"\']*)["\']?$',$v,$a3))
                            $prop[strtoupper($a3[1])]=$a3[2];
                    $this->OpenTag($tag,$prop);
                }
            }
        }
}

public function OpenTag($tag,$prop)
{
        //Balise ouvrante
        if($tag=='B' or $tag=='I' or $tag=='U')
            $this->SetStyle($tag,true);
        if($tag=='A')
            $this->HREF=$prop['HREF'];
        if($tag=='BR')
            $this->Ln(5);
        if($tag=='P')
            $this->ALIGN=$prop['ALIGN'];
        if($tag=='HR')
        {
if( !empty($prop['WIDTH']) )
                $Width = $prop['WIDTH'];
            else
                $Width = $this->w - $this->lMargin-$this->rMargin;
            $this->Ln(2);
            $x = $this->GetX();
            $y = $this->GetY();
            $this->SetLineWidth(0.4);
            $this->Line($x,$y,$x+$Width,$y);
            $this->SetLineWidth(0.2);
            $this->Ln(2);
        }
}

public function CloseTag($tag)
{
        //Balise fermante
        if($tag=='B' or $tag=='I' or $tag=='U')
            $this->SetStyle($tag,false);
        if($tag=='A')
            $this->HREF='';
        if($tag=='P')
            $this->ALIGN='';
}

public function SetStyle($tag,$enable)
{
        //Modifie le style et séctionne la police correspondante
        $this->$tag+=($enable ? 1 : -1);
        $style='';
        foreach(array('B','I','U') as $s)
            if($this->$s>0)
                $style.=$s;
        $this->SetFont('',$style);
}

public function PutLink($URL,$txt)
{
    //Place un hyperlien
    $this->SetTextColor(0,0,255);
    $this->SetStyle('U',true);
    $this->Write(5,$txt,$URL);
    $this->SetStyle('U',false);
    $this->SetTextColor(0);
}

public function Titre($titre)
{
    //Times 12
    $this->SetFont('Times','B',16);
    //Couleur de fond
    $this->SetFillColor(200,220,255);
    //Titre
    $this->Cell(0,6,"$titre",0,1,'L',1);
    //Saut de ligne
    $this->Ln(4);
}

    var $_toc=array();
    var $_numbering=false;
    var $_numberingFooter=false;
    var $_numPageNum=1;

public function AddPage($orientation='')
{
        parent::AddPage($orientation);
        if($this->_numbering)
            $this->_numPageNum++;
}

public function startPageNums()
{
        $this->_numbering=true;
        $this->_numberingFooter=true;
}

public function stopPageNums()
{
        $this->_numbering=false;
}

public function numPageNo()
{
        return $this->_numPageNum;
}

public function TOC_Entry($txt,$level=0)
{
        $this->_toc[]=array('t'=>$txt,'l'=>$level,'p'=>$this->numPageNo());
}

public function insertTOC($location=1,$labelSize=20,$entrySize=14,$tocfont='Times',$label='Table des matières')
{
        //make toc at end
        $this->stopPageNums();
        $this->AddPage();
        $tocstart=$this->page;

        $this->SetFont($tocfont,'B',$labelSize);
        $this->Cell(0,5,$label,0,1,'C');
        $this->Ln(20);

        foreach($this->_toc as $t) {

            //Offset
            $level=$t['l'];
            if($level>0)
                $this->Cell($level*8);
            $weight='';
            if($level==0)
                $weight='B';
            $str=$t['t'];
            $this->SetFont($tocfont,$weight,$entrySize);
            $strsize=$this->GetStringWidth($str);
            $this->Cell($strsize+2,$this->FontSize+2,$str);

            //Filling dots
            $this->SetFont($tocfont,'',$entrySize);
            $PageCellSize=$this->GetStringWidth($t['p'])+2;
            $w=$this->w-$this->lMargin-$this->rMargin-$PageCellSize-($level*8)-($strsize+2);
            $nb=$w/$this->GetStringWidth('.');
            $dots=str_repeat('.',$nb);
            $this->Cell($w,$this->FontSize+2,$dots,0,0,'R');

            //Page number
            $this->Cell($PageCellSize,$this->FontSize+2,$t['p'],0,1,'R');
        }

        //grab it and move to selected location
        $n=$this->page;
        $n_toc = $n - $tocstart + 1;
        $last = array();

        //store toc pages
        for($i = $tocstart;$i <= $n;$i++)
            $last[]=$this->pages[$i];

        //move pages
        for($i=$tocstart - 1;$i>=$location-1;$i--)
            $this->pages[$i+$n_toc]=$this->pages[$i];

        //Put toc pages at insert point
        for($i = 0;$i < $n_toc;$i++)
            $this->pages[$location + $i]=$last[$i];
}

public function Footer()
{
        if($this->_numberingFooter==false)
            return;
        //Go to 1.5 cm from bottom
        $this->SetY(-15);
	//Select Times italic 8
        $this->SetFont('Times','I',8);

        //$this->Cell(0,7,$this->numPageNo(),0,0,'C');
        $this->Line(20,282,190,282);
        $this->Cell(0,10,'Page '.$this->numPageNo().'/13',0,0,'C');

        if($this->_numbering==false)
                $this->_numberingFooter=false;
}

// End of class GLOSSAIRE
}

?>
