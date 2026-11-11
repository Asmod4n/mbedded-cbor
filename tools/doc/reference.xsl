<?xml version="1.0" encoding="UTF-8"?>
<!-- Writes the Markdown reference page of the class or free function that the parameter name names, from the Doxygen XML index.
     With the parameter part set to brief, it writes only the brief description of that page. -->
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:str="http://exslt.org/strings"
    xmlns:exsl="http://exslt.org/common"
    extension-element-prefixes="str exsl">
<xsl:output method="text" encoding="UTF-8"/>
<xsl:param name="name"/>
<xsl:param name="group"/>
<xsl:param name="part"/>
<xsl:variable name="nl" select="'&#10;'"/>
<xsl:variable name="indent_step" select="'    '"/>
<xsl:variable name="index" select="/"/>

<xsl:template match="/">
  <xsl:variable name="class" select="doxygenindex/compound[(@kind='class' or @kind='struct') and name=$name]"/>
  <xsl:variable name="function" select="doxygenindex/compound[@kind='namespace']/member[(@kind='function' or @kind='enum' or @kind='variable' or @kind='typedef') and concat(../name, '::', name)=$name]"/>
  <xsl:variable name="define" select="doxygenindex/compound[@kind='group']/member[@kind='define' and name=$name]"/>
  <xsl:variable name="member_name"><xsl:call-template name="last"><xsl:with-param name="s" select="$name"/></xsl:call-template></xsl:variable>
  <xsl:variable name="owner_name" select="substring($name, 1, string-length($name) - string-length($member_name) - 2)"/>
  <xsl:variable name="owner" select="doxygenindex/compound[(@kind='class' or @kind='struct') and name=$owner_name]"/>
  <xsl:choose>
    <xsl:when test="$class">
      <xsl:apply-templates select="document(concat($class/@refid, '.xml'), /)/doxygen/compounddef" mode="page"/>
    </xsl:when>
    <xsl:when test="$function">
      <xsl:variable name="id" select="$function[1]/@refid"/>
      <xsl:variable name="holders" select="doxygenindex/compound[member/@refid=$id]"/>
      <xsl:variable name="holder" select="($holders[@kind='group'] | $holders[@kind='namespace'][not($holders[@kind='group'])])[1]"/>
      <xsl:variable name="all" select="document(concat($holder/@refid, '.xml'), /)//memberdef[qualifiedname=$name]"/>
      <xsl:apply-templates select="($all[.//programlisting[@filename='.callseq']] | $all[normalize-space(briefdescription)])[1]" mode="page">
        <xsl:with-param name="all" select="$all"/>
      </xsl:apply-templates>
    </xsl:when>
    <xsl:when test="$define">
      <xsl:apply-templates select="document(concat($define[1]/../@refid, '.xml'), /)//memberdef[@id=$define[1]/@refid]" mode="page"/>
    </xsl:when>
    <xsl:when test="$owner">
      <xsl:variable name="c" select="document(concat($owner/@refid, '.xml'), /)/doxygen/compounddef"/>
      <xsl:variable name="all" select="$c/sectiondef[starts-with(@kind, 'public')]/memberdef[@kind='function' and name=$member_name][not(contains(argsstring, '=delete'))]"/>
      <xsl:apply-templates select="$all[.//programlisting[@filename='.callseq']][1]" mode="page">
        <xsl:with-param name="all" select="$all"/>
        <xsl:with-param name="owner" select="$c"/>
      </xsl:apply-templates>
    </xsl:when>
    <xsl:otherwise>
      <xsl:message terminate="yes">reference.xsl: no class, no enum, no macro, no member and no function has the name <xsl:value-of select="$name"/></xsl:message>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="compounddef|memberdef" mode="page">
  <xsl:param name="all" select="."/>
  <xsl:param name="owner" select="/.."/>
  <xsl:choose>
    <xsl:when test="$part = 'brief'">
      <xsl:call-template name="inline"><xsl:with-param name="node" select="briefdescription/para"/></xsl:call-template>
    </xsl:when>
    <xsl:otherwise>
      <xsl:apply-templates select="." mode="text">
        <xsl:with-param name="all" select="$all"/>
        <xsl:with-param name="owner" select="$owner"/>
      </xsl:apply-templates>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="compounddef|memberdef" mode="text">
  <xsl:param name="all"/>
  <xsl:param name="owner"/>
  <xsl:variable name="d" select="detaileddescription"/>
  <xsl:variable name="ds" select="$all/detaileddescription"/>
  <xsl:variable name="numbered" select="count($all[normalize-space(briefdescription)]) > 1"/>
  <xsl:text># </xsl:text>
  <xsl:choose>
    <xsl:when test="$owner">
      <xsl:call-template name="type"><xsl:with-param name="t" select="$owner/compoundname"/></xsl:call-template>
      <xsl:if test="$owner/templateparamlist/param">
        <xsl:text>&lt;</xsl:text>
        <xsl:apply-templates select="$owner/templateparamlist/param" mode="argument"/>
        <xsl:text>&gt;</xsl:text>
      </xsl:if>
      <xsl:value-of select="concat('::', name)"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:call-template name="type"><xsl:with-param name="t" select="compoundname|qualifiedname|self::memberdef[@kind='define']/name"/></xsl:call-template>
      <xsl:if test="self::compounddef and templateparamlist/param and not(contains(compoundname, '&lt;'))">
        <xsl:text>&lt;</xsl:text>
        <xsl:apply-templates select="templateparamlist/param" mode="argument"/>
        <xsl:text>&gt;</xsl:text>
      </xsl:if>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:value-of select="concat($nl, $nl, '```cpp', $nl)"/>
  <xsl:for-each select="$all">
    <xsl:if test="position() > 1 and (templateparamlist or preceding-sibling::memberdef[1]/templateparamlist)"><xsl:value-of select="$nl"/></xsl:if>
    <xsl:variable name="declaration"><xsl:apply-templates select="." mode="declaration"/></xsl:variable>
    <xsl:choose>
      <xsl:when test="$numbered">
        <xsl:value-of select="concat('// (', position(), ')', $nl, $declaration)"/>
      </xsl:when>
      <xsl:otherwise><xsl:value-of select="$declaration"/></xsl:otherwise>
    </xsl:choose>
  </xsl:for-each>
  <xsl:value-of select="concat('```', $nl)"/>
  <xsl:for-each select="$ds//programlisting[@filename='.callseq']">
    <xsl:call-template name="fence"><xsl:with-param name="info" select="'call-seq'"/></xsl:call-template>
  </xsl:for-each>
  <xsl:for-each select="$ds//programlisting[@filename='.example']">
    <xsl:call-template name="fence"><xsl:with-param name="info" select="'example'"/></xsl:call-template>
  </xsl:for-each>
  <xsl:for-each select="$ds//programlisting[@filename='.output']">
    <xsl:call-template name="fence"><xsl:with-param name="info" select="'output'"/></xsl:call-template>
  </xsl:for-each>
  <xsl:choose>
    <xsl:when test="$numbered">
      <xsl:for-each select="$all">
        <xsl:if test="normalize-space(briefdescription)">
          <xsl:apply-templates select="." mode="prose"><xsl:with-param name="number" select="concat('(', position(), ') ')"/></xsl:apply-templates>
        </xsl:if>
      </xsl:for-each>
    </xsl:when>
    <xsl:otherwise><xsl:apply-templates select="." mode="prose"/></xsl:otherwise>
  </xsl:choose>
  <xsl:if test="enumvalue[normalize-space(briefdescription) or normalize-space(detaileddescription)]">
    <xsl:value-of select="concat($nl, '| Value | Description |', $nl, '|---|---|', $nl)"/>
    <xsl:for-each select="enumvalue">
      <xsl:value-of select="concat('| `', name, '` | ')"/>
      <xsl:for-each select="briefdescription/para | detaileddescription/para">
        <xsl:if test="position() > 1"><xsl:text> </xsl:text></xsl:if>
        <xsl:call-template name="inline"><xsl:with-param name="node" select="."/></xsl:call-template>
      </xsl:for-each>
      <xsl:value-of select="concat(' |', $nl)"/>
    </xsl:for-each>
  </xsl:if>
  <xsl:variable name="constants" select="sectiondef[starts-with(@kind, 'public')]/memberdef[@kind='variable'][normalize-space(briefdescription)]"/>
  <xsl:if test="$constants">
    <xsl:value-of select="concat($nl, '| Member | Description |', $nl, '|---|---|', $nl)"/>
    <xsl:for-each select="$constants">
      <xsl:value-of select="concat('| `', name, '` | ')"/>
      <xsl:call-template name="inline"><xsl:with-param name="node" select="briefdescription/para"/></xsl:call-template>
      <xsl:value-of select="concat(' |', $nl)"/>
    </xsl:for-each>
  </xsl:if>
  <xsl:variable name="params_all" select="$ds//parameterlist[@kind='templateparam' or @kind='param']/parameteritem"/>
  <xsl:variable name="params" select="$params_all[not(normalize-space(parameternamelist/parametername) = preceding::parameteritem[count(. | $params_all) = count($params_all)]/parameternamelist/parametername)]"/>
  <xsl:if test="$params">
    <xsl:value-of select="concat($nl, '## Parameters', $nl, $nl)"/>
    <xsl:for-each select="$params">
      <xsl:value-of select="concat('- `', normalize-space(parameternamelist/parametername), '`: ')"/>
      <xsl:call-template name="inline"><xsl:with-param name="node" select="parameterdescription/para"/></xsl:call-template>
      <xsl:value-of select="$nl"/>
    </xsl:for-each>
  </xsl:if>
  <xsl:choose>
    <xsl:when test="$numbered and $ds//simplesect[@kind='return']">
      <xsl:value-of select="concat($nl, '## Return value', $nl, $nl)"/>
      <xsl:for-each select="$all">
        <xsl:variable name="r" select="normalize-space(detaileddescription//simplesect[@kind='return'])"/>
        <xsl:variable name="p" select="position()"/>
        <xsl:variable name="same">
          <xsl:for-each select="$all">
            <xsl:if test="normalize-space(detaileddescription//simplesect[@kind='return']) = $r">
              <xsl:value-of select="concat(position(), ' ')"/>
            </xsl:if>
          </xsl:for-each>
        </xsl:variable>
        <xsl:if test="$r != '' and substring-before($same, ' ') = string($p)">
          <xsl:if test="$p > 1"><xsl:value-of select="$nl"/></xsl:if>
          <xsl:value-of select="concat('(', str:replace(normalize-space($same), ' ', ', '), ') ')"/>
          <xsl:call-template name="inline"><xsl:with-param name="node" select="detaileddescription//simplesect[@kind='return']/para"/></xsl:call-template>
          <xsl:value-of select="$nl"/>
        </xsl:if>
      </xsl:for-each>
    </xsl:when>
    <xsl:otherwise>
      <xsl:for-each select="$d//simplesect[@kind='return']">
        <xsl:value-of select="concat($nl, '## Return value', $nl, $nl)"/>
        <xsl:call-template name="inline"><xsl:with-param name="node" select="para"/></xsl:call-template>
        <xsl:value-of select="$nl"/>
      </xsl:for-each>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:variable name="retvals_all" select="$ds//parameterlist[@kind='retval']/parameteritem"/>
  <xsl:variable name="retvals" select="$retvals_all[not(normalize-space(parameternamelist/parametername) = preceding::parameteritem[count(. | $retvals_all) = count($retvals_all)]/parameternamelist/parametername)]"/>
  <xsl:variable name="error_paras_all" select="$ds//*[title='Errors']/para[not(ancestor::simplesect)][text()[normalize-space()] or computeroutput or ref or ulink or bold or emphasis]"/>
  <xsl:variable name="error_paras" select="$error_paras_all[not(normalize-space(.) = preceding::para[count(. | $error_paras_all) = count($error_paras_all)])]"/>
  <xsl:variable name="paged" select="sectiondef[starts-with(@kind, 'public')]/memberdef[@kind='function'][.//programlisting[@filename='.callseq']]/name"/>
  <xsl:variable name="throws_all" select="$ds//parameterlist[@kind='exception']/parameteritem | sectiondef[starts-with(@kind, 'public')]/memberdef[not(name = $paged)]/detaileddescription//parameterlist[@kind='exception']/parameteritem"/>
  <xsl:variable name="throws" select="$throws_all[not(string(parameterdescription) = preceding::parameteritem[count(. | $throws_all) = count($throws_all)]/parameterdescription)]"/>
  <xsl:if test="$retvals or $throws or $error_paras">
    <xsl:value-of select="concat($nl, '## Errors', $nl, $nl)"/>
    <xsl:for-each select="$retvals">
      <xsl:text>- </xsl:text>
      <xsl:for-each select="parameternamelist/parametername">
        <xsl:if test="position() > 1">, </xsl:if>
        <xsl:value-of select="concat('`', normalize-space(.), '`')"/>
      </xsl:for-each>
      <xsl:text>: </xsl:text>
      <xsl:call-template name="inline"><xsl:with-param name="node" select="parameterdescription/para"/></xsl:call-template>
      <xsl:value-of select="$nl"/>
    </xsl:for-each>
    <xsl:for-each select="$throws">
      <xsl:value-of select="concat('- `', normalize-space(parameternamelist/parametername), '`: ')"/>
      <xsl:call-template name="inline"><xsl:with-param name="node" select="parameterdescription/para"/></xsl:call-template>
      <xsl:value-of select="$nl"/>
    </xsl:for-each>
    <xsl:for-each select="$error_paras">
      <xsl:if test="position() > 1"><xsl:value-of select="$nl"/></xsl:if>
      <xsl:call-template name="inline"><xsl:with-param name="node" select="."/></xsl:call-template>
      <xsl:value-of select="$nl"/>
    </xsl:for-each>
  </xsl:if>
  <xsl:for-each select="$d//simplesect[@kind='par'][title='Complexity']">
    <xsl:value-of select="concat($nl, '## Cost', $nl, $nl)"/>
    <xsl:call-template name="inline"><xsl:with-param name="node" select="para"/></xsl:call-template>
    <xsl:value-of select="$nl"/>
  </xsl:for-each>
  <xsl:for-each select="$d//programlisting[not(starts-with(@filename, '.'))]">
    <xsl:if test="position() = 1"><xsl:value-of select="concat($nl, '## Example', $nl)"/></xsl:if>
    <xsl:call-template name="fence"><xsl:with-param name="info" select="'cpp'"/></xsl:call-template>
  </xsl:for-each>
  <xsl:variable name="see_all" select="$ds//simplesect[@kind='see']"/>
  <xsl:variable name="see" select="$see_all[not(normalize-space(.) = preceding::simplesect[count(. | $see_all) = count($see_all)])]"/>
  <xsl:if test="$see">
    <xsl:value-of select="concat($nl, '## See also', $nl, $nl)"/>
    <xsl:for-each select="$see">
      <xsl:if test="position() > 1"> · </xsl:if>
      <xsl:call-template name="inline"><xsl:with-param name="node" select="para"/></xsl:call-template>
    </xsl:for-each>
    <xsl:value-of select="$nl"/>
  </xsl:if>
</xsl:template>

<xsl:template match="compounddef|memberdef" mode="prose">
  <xsl:param name="number" select="''"/>
  <xsl:variable name="paras" select="detaileddescription//para[not(ancestor::simplesect or ancestor::parameterlist or ancestor::sect1 or ancestor::sect2 or ancestor::sect3 or ancestor::table or ancestor::itemizedlist)][text()[normalize-space()] or computeroutput or ref or ulink or bold or emphasis or table or itemizedlist]"/>
  <xsl:for-each select="$paras">
    <xsl:value-of select="$nl"/>
    <xsl:variable name="own"><xsl:call-template name="inline"><xsl:with-param name="node" select="."/></xsl:call-template></xsl:variable>
    <xsl:variable name="brief">
      <xsl:if test="position() = 1 and normalize-space(../../briefdescription)">
        <xsl:call-template name="inline"><xsl:with-param name="node" select="../../briefdescription/para"/></xsl:call-template>
      </xsl:if>
    </xsl:variable>
    <xsl:variable name="text" select="normalize-space(concat(substring($number, 1, string-length($number) * (position() = 1)), $brief, ' ', $own))"/>
    <xsl:if test="$text != ''"><xsl:value-of select="concat($text, $nl)"/></xsl:if>
    <xsl:for-each select="itemizedlist">
      <xsl:if test="$text != '' or position() > 1"><xsl:value-of select="$nl"/></xsl:if>
      <xsl:for-each select="listitem">
        <xsl:text>- </xsl:text>
        <xsl:call-template name="inline"><xsl:with-param name="node" select="para"/></xsl:call-template>
        <xsl:value-of select="$nl"/>
      </xsl:for-each>
    </xsl:for-each>
    <xsl:for-each select="table">
      <xsl:if test="$text != '' or position() > 1"><xsl:value-of select="$nl"/></xsl:if>
      <xsl:for-each select="row">
        <xsl:text>|</xsl:text>
        <xsl:for-each select="entry">
          <xsl:text> </xsl:text>
          <xsl:call-template name="inline"><xsl:with-param name="node" select="para"/></xsl:call-template>
          <xsl:text> |</xsl:text>
        </xsl:for-each>
        <xsl:value-of select="$nl"/>
        <xsl:if test="entry/@thead='yes'">
          <xsl:text>|</xsl:text>
          <xsl:for-each select="entry">---|</xsl:for-each>
          <xsl:value-of select="$nl"/>
        </xsl:if>
      </xsl:for-each>
    </xsl:for-each>
  </xsl:for-each>
  <xsl:if test="$number != '' and not($paras)">
    <xsl:value-of select="$nl"/>
    <xsl:value-of select="$number"/>
    <xsl:call-template name="inline"><xsl:with-param name="node" select="briefdescription/para"/></xsl:call-template>
    <xsl:value-of select="$nl"/>
  </xsl:if>
</xsl:template>

<xsl:template name="inline">
  <xsl:param name="node"/>
  <xsl:variable name="text"><xsl:apply-templates select="$node/node()" mode="md"/></xsl:variable>
  <xsl:value-of select="normalize-space($text)"/>
</xsl:template>

<xsl:template match="text()" mode="md"><xsl:value-of select="."/></xsl:template>
<xsl:template match="computeroutput" mode="md">
  <xsl:value-of select="concat('`', ., '`')"/>
</xsl:template>
<xsl:template match="bold" mode="md"><xsl:text>**</xsl:text><xsl:apply-templates mode="md"/><xsl:text>**</xsl:text></xsl:template>
<xsl:template match="emphasis" mode="md"><xsl:text>*</xsl:text><xsl:apply-templates mode="md"/><xsl:text>*</xsl:text></xsl:template>
<xsl:template match="ulink" mode="md">
  <xsl:value-of select="concat('[', ., '](', @url, ')')"/>
</xsl:template>
<xsl:template match="ref" mode="md">
  <xsl:variable name="target"><xsl:call-template name="page_of"><xsl:with-param name="refid" select="@refid"/></xsl:call-template></xsl:variable>
  <xsl:variable name="target_group"><xsl:call-template name="group_of"><xsl:with-param name="refid" select="@refid"/></xsl:call-template></xsl:variable>
  <xsl:variable name="label" select="str:replace(str:replace(string(.), '[', '\['), ']', '\]')"/>
  <xsl:choose>
    <xsl:when test="$target_group = '' or $target_group = $group"><xsl:value-of select="concat('[', $label, '](', $target, '.md)')"/></xsl:when>
    <xsl:otherwise><xsl:value-of select="concat('[', $label, '](../', $target_group, '/', $target, '.md)')"/></xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template name="group_of">
  <xsl:param name="refid"/>
  <xsl:variable name="member" select="$index/doxygenindex/compound/member[@refid=$refid]"/>
  <xsl:variable name="class_id">
    <xsl:choose>
      <xsl:when test="$member and ($member/../@kind='class' or $member/../@kind='struct')"><xsl:value-of select="$member/../@refid"/></xsl:when>
      <xsl:otherwise><xsl:value-of select="$refid"/></xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:for-each select="$index/doxygenindex/compound[@kind='group']">
    <xsl:if test="member/@refid = $refid or document(concat(@refid, '.xml'), $index)/doxygen/compounddef/innerclass/@refid = $class_id">
      <xsl:value-of select="name"/>
    </xsl:if>
  </xsl:for-each>
</xsl:template>
<xsl:template match="simplesect|parameterlist|programlisting|sect1|sect2|sect3|title|table|itemizedlist" mode="md"/>

<xsl:template name="page_of">
  <xsl:param name="refid"/>
  <xsl:variable name="compound" select="$index/doxygenindex/compound[@refid=$refid]"/>
  <xsl:variable name="member" select="$index/doxygenindex/compound/member[@refid=$refid]"/>
  <xsl:choose>
    <xsl:when test="$compound"><xsl:call-template name="last"><xsl:with-param name="s" select="normalize-space(str:split(concat($compound/name, '&lt;'), '&lt;')[1])"/></xsl:call-template></xsl:when>
    <xsl:when test="($member/../@kind='class' or $member/../@kind='struct') and document(concat($member/../@refid, '.xml'), $index)//memberdef[name=$member/name][.//programlisting[@filename='.callseq']]">
      <xsl:choose>
        <xsl:when test="$member/name = 'operator[]'">operator_subscript</xsl:when>
        <xsl:otherwise><xsl:value-of select="$member/name"/></xsl:otherwise>
      </xsl:choose>
    </xsl:when>
    <xsl:when test="$member/../@kind='class' or $member/../@kind='struct'"><xsl:call-template name="last"><xsl:with-param name="s" select="$member/../name"/></xsl:call-template></xsl:when>
    <xsl:otherwise><xsl:value-of select="$member/name"/></xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template name="last">
  <xsl:param name="s"/>
  <xsl:choose>
    <xsl:when test="contains($s, '::')"><xsl:call-template name="last"><xsl:with-param name="s" select="substring-after($s, '::')"/></xsl:call-template></xsl:when>
    <xsl:otherwise><xsl:value-of select="$s"/></xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template name="fence">
  <xsl:param name="info"/>
  <xsl:variable name="lines">
    <xsl:for-each select="codeline"><l><xsl:apply-templates mode="code"/></l></xsl:for-each>
  </xsl:variable>
  <xsl:variable name="first" select="string(exsl:node-set($lines)/l[1])"/>
  <xsl:variable name="indent" select="substring-before($first, substring(normalize-space($first), 1, 1))"/>
  <xsl:value-of select="concat($nl, '```', $info, $nl)"/>
  <xsl:for-each select="exsl:node-set($lines)/l">
    <xsl:choose>
      <xsl:when test="starts-with(., $indent)"><xsl:value-of select="substring(., string-length($indent) + 1)"/></xsl:when>
      <xsl:otherwise><xsl:value-of select="."/></xsl:otherwise>
    </xsl:choose>
    <xsl:value-of select="$nl"/>
  </xsl:for-each>
  <xsl:value-of select="concat('```', $nl)"/>
</xsl:template>

<xsl:template match="text()" mode="code"><xsl:value-of select="."/></xsl:template>
<xsl:template match="sp" mode="code"><xsl:text> </xsl:text></xsl:template>

<xsl:template name="type">
  <xsl:param name="t"/>
  <xsl:value-of select="str:replace(str:replace(str:replace(normalize-space($t), '&lt; ', '&lt;'), ' &gt;', '&gt;'), 'std::basic_string_view&lt;char&gt;', 'std::string_view')"/>
</xsl:template>

<xsl:template name="joined">
  <xsl:param name="left"/>
  <xsl:param name="right"/>
  <xsl:value-of select="$left"/>
  <xsl:if test="$left != '' and not(substring($left, string-length($left)) = '&amp;' or substring($left, string-length($left)) = '*')">
    <xsl:text> </xsl:text>
  </xsl:if>
  <xsl:value-of select="$right"/>
</xsl:template>

<xsl:template match="param" mode="declaration">
  <xsl:variable name="full"><xsl:call-template name="type"><xsl:with-param name="t" select="type"/></xsl:call-template></xsl:variable>
  <xsl:variable name="t">
    <xsl:choose>
      <xsl:when test="substring($full, string-length($full) - 5) = ' const'"><xsl:value-of select="substring($full, 1, string-length($full) - 6)"/></xsl:when>
      <xsl:otherwise><xsl:value-of select="$full"/></xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:if test="position() > 1">, </xsl:if>
  <xsl:choose>
    <xsl:when test="array and substring($t, string-length($t)) = ')'">
      <xsl:value-of select="concat(substring($t, 1, string-length($t) - 1), declname, ')', array)"/>
    </xsl:when>
    <xsl:when test="declname">
      <xsl:call-template name="joined"><xsl:with-param name="left" select="$t"/><xsl:with-param name="right" select="declname"/></xsl:call-template>
    </xsl:when>
    <xsl:otherwise><xsl:value-of select="$t"/></xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="memberdef[@kind='function' or @kind='friend']" mode="declaration">
  <xsl:param name="indent" select="''"/>
  <xsl:if test="templateparamlist">
    <xsl:value-of select="concat($indent, 'template &lt;')"/>
    <xsl:apply-templates select="templateparamlist/param" mode="template"/>
    <xsl:value-of select="concat('&gt;', $nl)"/>
  </xsl:if>
  <xsl:value-of select="$indent"/>
  <xsl:if test="@kind='friend'">friend </xsl:if>
  <xsl:if test="@static='yes'">static </xsl:if>
  <xsl:if test="@explicit='yes'">explicit </xsl:if>
  <xsl:if test="@consteval='yes' and normalize-space(type) != 'consteval'">consteval </xsl:if>
  <xsl:if test="@constexpr='yes'">constexpr </xsl:if>
  <xsl:variable name="t"><xsl:call-template name="type"><xsl:with-param name="t" select="type"/></xsl:call-template></xsl:variable>
  <xsl:call-template name="joined"><xsl:with-param name="left" select="$t"/><xsl:with-param name="right" select="name"/></xsl:call-template>
  <xsl:text>(</xsl:text>
  <xsl:apply-templates select="param" mode="declaration"/>
  <xsl:text>)</xsl:text>
  <xsl:if test="@const='yes'"> const</xsl:if>
  <xsl:if test="@refqual='lvalue'"> &amp;</xsl:if>
  <xsl:if test="@refqual='rvalue'"> &amp;&amp;</xsl:if>
  <xsl:choose>
    <xsl:when test="@noexceptexpression">
      <xsl:text> noexcept(</xsl:text>
      <xsl:call-template name="type"><xsl:with-param name="t" select="str:replace(@noexceptexpression, '&amp;&amp;', ' &amp;&amp; ')"/></xsl:call-template>
      <xsl:text>)</xsl:text>
    </xsl:when>
    <xsl:when test="@noexcept='yes'"> noexcept</xsl:when>
  </xsl:choose>
  <xsl:if test="contains(argsstring, ' override')"> override</xsl:if>
  <xsl:if test="requiresclause">
    <xsl:value-of select="concat($nl, $indent, $indent_step, 'requires ', normalize-space(requiresclause))"/>
  </xsl:if>
  <xsl:value-of select="concat(';', $nl)"/>
</xsl:template>

<xsl:template match="param" mode="template">
  <xsl:if test="position() > 1">, </xsl:if>
  <xsl:value-of select="normalize-space(concat(type, ' ', declname))"/>
</xsl:template>

<xsl:template match="param" mode="argument">
  <xsl:if test="position() > 1">, </xsl:if>
  <xsl:choose>
    <xsl:when test="declname"><xsl:value-of select="declname"/></xsl:when>
    <xsl:otherwise><xsl:value-of select="str:split(normalize-space(type), ' ')[last()]"/></xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="memberdef[@kind='define']" mode="declaration">
  <xsl:value-of select="concat('#define ', name)"/>
  <xsl:if test="param">
    <xsl:text>(</xsl:text>
    <xsl:for-each select="param">
      <xsl:if test="position() > 1">, </xsl:if>
      <xsl:value-of select="defname"/>
    </xsl:for-each>
    <xsl:text>)</xsl:text>
  </xsl:if>
  <xsl:value-of select="concat(' ', normalize-space(initializer), $nl)"/>
</xsl:template>

<xsl:template match="memberdef[@kind='variable']" mode="declaration">
  <xsl:param name="indent" select="''"/>
  <xsl:variable name="t"><xsl:call-template name="type"><xsl:with-param name="t" select="type"/></xsl:call-template></xsl:variable>
  <xsl:if test="templateparamlist">
    <xsl:value-of select="concat($indent, 'template &lt;')"/>
    <xsl:apply-templates select="templateparamlist/param" mode="template"/>
    <xsl:value-of select="concat('&gt;', $nl)"/>
  </xsl:if>
  <xsl:value-of select="$indent"/>
  <xsl:if test="@inline='yes'">inline </xsl:if>
  <xsl:if test="@static='yes'">static </xsl:if>
  <xsl:if test="@constexpr='yes'">constexpr </xsl:if>
  <xsl:value-of select="concat($t, ' ', name)"/>
  <xsl:if test="initializer"><xsl:value-of select="concat(' ', normalize-space(initializer))"/></xsl:if>
  <xsl:value-of select="concat(';', $nl)"/>
</xsl:template>

<xsl:template match="memberdef[@kind='enum']" mode="declaration">
  <xsl:text>enum </xsl:text>
  <xsl:if test="@strong='yes'">class </xsl:if>
  <xsl:value-of select="concat(name, ' {', $nl)"/>
  <xsl:for-each select="enumvalue">
    <xsl:value-of select="concat($indent_step, name)"/>
    <xsl:if test="initializer"><xsl:value-of select="concat(' ', normalize-space(initializer))"/></xsl:if>
    <xsl:if test="position() != last()">,</xsl:if>
    <xsl:value-of select="$nl"/>
  </xsl:for-each>
  <xsl:value-of select="concat('};', $nl)"/>
</xsl:template>

<xsl:template match="memberdef[@kind='typedef']" mode="declaration">
  <xsl:param name="indent" select="''"/>
  <xsl:variable name="t"><xsl:call-template name="type"><xsl:with-param name="t" select="type"/></xsl:call-template></xsl:variable>
  <xsl:value-of select="concat($indent, 'using ', name, ' = ', $t, ';', $nl)"/>
</xsl:template>

<xsl:template name="members">
  <xsl:param name="c"/>
  <xsl:param name="indent"/>
  <xsl:variable name="body" select="$c/location"/>
  <xsl:variable name="public" select="$c/sectiondef[starts-with(@kind, 'public')]/memberdef[not(contains(argsstring, '=delete'))][not($body/@bodystart) or (location/@file = $body/@file and location/@line &gt;= $body/@bodystart and location/@line &lt;= $body/@bodyend)]"/>
  <xsl:variable name="short" select="$c/compoundname"/>
  <xsl:variable name="ctor_name"><xsl:call-template name="last"><xsl:with-param name="s" select="$short"/></xsl:call-template></xsl:variable>
  <xsl:variable name="types" select="$public[@kind='typedef']"/>
  <xsl:variable name="variables" select="$public[@kind='variable']"/>
  <xsl:variable name="ctors" select="$public[@kind='function' and name=$ctor_name]"/>
  <xsl:variable name="nested" select="$c/innerclass[@prot='public']"/>
  <xsl:variable name="functions" select="$public[@kind='function' and name!=$ctor_name]"/>
  <xsl:variable name="friends" select="$c/sectiondef[@kind='friend']/memberdef[@kind='friend' and @prot='public' and normalize-space(argsstring)]"/>
  <xsl:apply-templates select="$types" mode="declaration"><xsl:with-param name="indent" select="$indent"/></xsl:apply-templates>
  <xsl:if test="$types and ($variables or $ctors or $nested or $functions)"><xsl:value-of select="$nl"/></xsl:if>
  <xsl:apply-templates select="$variables" mode="declaration"><xsl:with-param name="indent" select="$indent"/></xsl:apply-templates>
  <xsl:if test="$variables and ($ctors or $nested or $functions)"><xsl:value-of select="$nl"/></xsl:if>
  <xsl:for-each select="$ctors">
    <xsl:apply-templates select="." mode="declaration"><xsl:with-param name="indent" select="$indent"/></xsl:apply-templates>
    <xsl:if test="templateparamlist or (position() = last() and ($nested or $functions or $friends))"><xsl:value-of select="$nl"/></xsl:if>
  </xsl:for-each>
  <xsl:for-each select="$nested">
    <xsl:variable name="inner" select="document(concat(@refid, '.xml'), $index)/doxygen/compounddef"/>
    <xsl:variable name="inner_name"><xsl:call-template name="last"><xsl:with-param name="s" select="$inner/compoundname"/></xsl:call-template></xsl:variable>
    <xsl:value-of select="concat($indent, $inner/@kind, ' ', $inner_name, ' {', $nl)"/>
    <xsl:if test="$inner/@kind = 'class'"><xsl:value-of select="concat($indent, 'public:', $nl)"/></xsl:if>
    <xsl:call-template name="members">
      <xsl:with-param name="c" select="$inner"/>
      <xsl:with-param name="indent" select="concat($indent, $indent_step)"/>
    </xsl:call-template>
    <xsl:value-of select="concat($indent, '};', $nl)"/>
    <xsl:if test="$functions"><xsl:value-of select="$nl"/></xsl:if>
  </xsl:for-each>
  <xsl:for-each select="$functions">
    <xsl:if test="position() > 1 and (templateparamlist or preceding-sibling::memberdef[1]/templateparamlist) and name != preceding-sibling::memberdef[1]/name"><xsl:value-of select="$nl"/></xsl:if>
    <xsl:apply-templates select="." mode="declaration"><xsl:with-param name="indent" select="$indent"/></xsl:apply-templates>
  </xsl:for-each>
  <xsl:if test="$friends and ($ctors or $nested or $functions)"><xsl:value-of select="$nl"/></xsl:if>
  <xsl:apply-templates select="$friends" mode="declaration"><xsl:with-param name="indent" select="$indent"/></xsl:apply-templates>
</xsl:template>

<xsl:template match="compounddef" mode="declaration">
  <xsl:variable name="n">
    <xsl:choose>
      <xsl:when test="starts-with(compoundname, 'cbor::')"><xsl:call-template name="last"><xsl:with-param name="s" select="compoundname"/></xsl:call-template></xsl:when>
      <xsl:otherwise><xsl:call-template name="type"><xsl:with-param name="t" select="compoundname"/></xsl:call-template></xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:if test="templateparamlist">
    <xsl:text>template &lt;</xsl:text>
    <xsl:apply-templates select="templateparamlist/param" mode="template"/>
    <xsl:value-of select="concat('&gt;', $nl)"/>
  </xsl:if>
  <xsl:value-of select="concat(@kind, ' ', $n)"/>
  <xsl:for-each select="basecompoundref">
    <xsl:choose>
      <xsl:when test="position() = 1"> : </xsl:when>
      <xsl:otherwise>, </xsl:otherwise>
    </xsl:choose>
    <xsl:if test="../@kind = 'class'"><xsl:value-of select="concat(@prot, ' ')"/></xsl:if>
    <xsl:value-of select="."/>
  </xsl:for-each>
  <xsl:choose>
    <xsl:when test="sectiondef[starts-with(@kind, 'public')]/memberdef or innerclass[@prot='public']">
      <xsl:value-of select="concat($nl, '{', $nl)"/>
      <xsl:if test="@kind = 'class'"><xsl:value-of select="concat('public:', $nl)"/></xsl:if>
      <xsl:call-template name="members">
        <xsl:with-param name="c" select="."/>
        <xsl:with-param name="indent" select="$indent_step"/>
      </xsl:call-template>
      <xsl:value-of select="concat('};', $nl)"/>
    </xsl:when>
    <xsl:when test="basecompoundref"><xsl:value-of select="concat(' {};', $nl)"/></xsl:when>
    <xsl:otherwise><xsl:value-of select="concat(';', $nl)"/></xsl:otherwise>
  </xsl:choose>
</xsl:template>

</xsl:stylesheet>
