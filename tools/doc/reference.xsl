<?xml version="1.0" encoding="UTF-8"?>
<!-- Writes the Markdown reference page of the class or free function that the parameter name names, from the Doxygen XML index. -->
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:str="http://exslt.org/strings"
    xmlns:exsl="http://exslt.org/common"
    extension-element-prefixes="str exsl">
<xsl:output method="text" encoding="UTF-8"/>
<xsl:param name="name"/>
<xsl:variable name="nl" select="'&#10;'"/>
<xsl:variable name="indent_step" select="'    '"/>
<xsl:variable name="index" select="/"/>

<xsl:template match="/">
  <xsl:variable name="class" select="doxygenindex/compound[@kind='class' and name=$name]"/>
  <xsl:variable name="function" select="doxygenindex/compound[@kind='namespace']/member[@kind='function' and concat(../name, '::', name)=$name]"/>
  <xsl:choose>
    <xsl:when test="$class">
      <xsl:apply-templates select="document(concat($class/@refid, '.xml'), /)/doxygen/compounddef" mode="page"/>
    </xsl:when>
    <xsl:when test="$function">
      <xsl:apply-templates select="document(concat($function/../@refid, '.xml'), /)//memberdef[@id=$function/@refid]" mode="page"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:message terminate="yes">reference.xsl: no class and no function has the name <xsl:value-of select="$name"/></xsl:message>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="compounddef|memberdef" mode="page">
  <xsl:variable name="d" select="detaileddescription"/>
  <xsl:text># </xsl:text>
  <xsl:value-of select="compoundname|qualifiedname"/>
  <xsl:value-of select="concat($nl, $nl, '```cpp', $nl)"/>
  <xsl:apply-templates select="." mode="declaration"/>
  <xsl:value-of select="concat('```', $nl)"/>
  <xsl:for-each select="$d//programlisting[@filename='.callseq']">
    <xsl:call-template name="fence"><xsl:with-param name="info" select="'call-seq'"/></xsl:call-template>
  </xsl:for-each>
  <xsl:for-each select="$d//programlisting[@filename='.example']">
    <xsl:call-template name="fence"><xsl:with-param name="info" select="'example'"/></xsl:call-template>
  </xsl:for-each>
  <xsl:variable name="paras" select="$d//para[not(ancestor::simplesect or ancestor::parameterlist or ancestor::sect1 or ancestor::sect2 or ancestor::sect3)][text()[normalize-space()] or computeroutput or ref or ulink or bold or emphasis]"/>
  <xsl:for-each select="$paras">
    <xsl:value-of select="$nl"/>
    <xsl:if test="position() = 1 and normalize-space(../../briefdescription)">
      <xsl:call-template name="inline"><xsl:with-param name="node" select="../../briefdescription/para"/></xsl:call-template>
      <xsl:text> </xsl:text>
    </xsl:if>
    <xsl:call-template name="inline"><xsl:with-param name="node" select="."/></xsl:call-template>
    <xsl:value-of select="$nl"/>
  </xsl:for-each>
  <xsl:variable name="params" select="$d//parameterlist[@kind='templateparam' or @kind='param']/parameteritem"/>
  <xsl:if test="$params">
    <xsl:value-of select="concat($nl, '## Parameters', $nl, $nl)"/>
    <xsl:for-each select="$params">
      <xsl:value-of select="concat('- `', normalize-space(parameternamelist/parametername), '`: ')"/>
      <xsl:call-template name="inline"><xsl:with-param name="node" select="parameterdescription/para"/></xsl:call-template>
      <xsl:value-of select="$nl"/>
    </xsl:for-each>
  </xsl:if>
  <xsl:for-each select="$d//simplesect[@kind='return']">
    <xsl:value-of select="concat($nl, '## Return value', $nl, $nl)"/>
    <xsl:call-template name="inline"><xsl:with-param name="node" select="para"/></xsl:call-template>
    <xsl:value-of select="$nl"/>
  </xsl:for-each>
  <xsl:variable name="retvals" select="$d//parameterlist[@kind='retval']/parameteritem"/>
  <xsl:variable name="error_paras" select="$d//*[title='Errors']/para[not(ancestor::simplesect)][text()[normalize-space()] or computeroutput or ref or ulink or bold or emphasis]"/>
  <xsl:if test="$retvals or $error_paras">
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
    <xsl:value-of select="concat($nl, '## Example', $nl)"/>
    <xsl:call-template name="fence"><xsl:with-param name="info" select="'cpp'"/></xsl:call-template>
  </xsl:for-each>
  <xsl:variable name="see" select="$d//simplesect[@kind='see']"/>
  <xsl:if test="$see">
    <xsl:value-of select="concat($nl, '## See also', $nl, $nl)"/>
    <xsl:for-each select="$see">
      <xsl:if test="position() > 1"> · </xsl:if>
      <xsl:call-template name="inline"><xsl:with-param name="node" select="para"/></xsl:call-template>
    </xsl:for-each>
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
  <xsl:value-of select="concat('[', ., '](', $target, '.md)')"/>
</xsl:template>
<xsl:template match="simplesect|parameterlist|programlisting|sect1|sect2|sect3|title" mode="md"/>

<xsl:template name="page_of">
  <xsl:param name="refid"/>
  <xsl:variable name="compound" select="$index/doxygenindex/compound[@refid=$refid]"/>
  <xsl:variable name="member" select="$index/doxygenindex/compound/member[@refid=$refid]"/>
  <xsl:choose>
    <xsl:when test="$compound"><xsl:call-template name="last"><xsl:with-param name="s" select="$compound/name"/></xsl:call-template></xsl:when>
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
  <xsl:value-of select="str:replace(str:replace(normalize-space($t), '&lt; ', '&lt;'), ' &gt;', '&gt;')"/>
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
    <xsl:when test="declname">
      <xsl:call-template name="joined"><xsl:with-param name="left" select="$t"/><xsl:with-param name="right" select="declname"/></xsl:call-template>
    </xsl:when>
    <xsl:otherwise><xsl:value-of select="$t"/></xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="memberdef[@kind='function']" mode="declaration">
  <xsl:param name="indent" select="''"/>
  <xsl:if test="templateparamlist">
    <xsl:value-of select="concat($indent, 'template &lt;')"/>
    <xsl:for-each select="templateparamlist/param">
      <xsl:if test="position() > 1">, </xsl:if>
      <xsl:value-of select="concat(type, ' ', declname)"/>
    </xsl:for-each>
    <xsl:value-of select="concat('&gt;', $nl)"/>
  </xsl:if>
  <xsl:value-of select="$indent"/>
  <xsl:if test="@explicit='yes'">explicit </xsl:if>
  <xsl:variable name="t"><xsl:call-template name="type"><xsl:with-param name="t" select="type"/></xsl:call-template></xsl:variable>
  <xsl:call-template name="joined"><xsl:with-param name="left" select="$t"/><xsl:with-param name="right" select="name"/></xsl:call-template>
  <xsl:text>(</xsl:text>
  <xsl:apply-templates select="param" mode="declaration"/>
  <xsl:text>)</xsl:text>
  <xsl:if test="@const='yes'"> const</xsl:if>
  <xsl:value-of select="concat(';', $nl)"/>
</xsl:template>

<xsl:template match="memberdef[@kind='typedef']" mode="declaration">
  <xsl:param name="indent" select="''"/>
  <xsl:variable name="t"><xsl:call-template name="type"><xsl:with-param name="t" select="type"/></xsl:call-template></xsl:variable>
  <xsl:value-of select="concat($indent, 'using ', name, ' = ', $t, ';', $nl)"/>
</xsl:template>

<xsl:template name="members">
  <xsl:param name="c"/>
  <xsl:param name="indent"/>
  <xsl:variable name="public" select="$c/sectiondef[starts-with(@kind, 'public')]/memberdef"/>
  <xsl:variable name="short" select="$c/compoundname"/>
  <xsl:variable name="ctor_name"><xsl:call-template name="last"><xsl:with-param name="s" select="$short"/></xsl:call-template></xsl:variable>
  <xsl:variable name="types" select="$public[@kind='typedef']"/>
  <xsl:variable name="ctors" select="$public[@kind='function' and name=$ctor_name]"/>
  <xsl:variable name="nested" select="$c/innerclass[@prot='public']"/>
  <xsl:variable name="functions" select="$public[@kind='function' and name!=$ctor_name]"/>
  <xsl:apply-templates select="$types" mode="declaration"><xsl:with-param name="indent" select="$indent"/></xsl:apply-templates>
  <xsl:if test="$types and ($ctors or $nested or $functions)"><xsl:value-of select="$nl"/></xsl:if>
  <xsl:for-each select="$ctors">
    <xsl:apply-templates select="." mode="declaration"><xsl:with-param name="indent" select="$indent"/></xsl:apply-templates>
    <xsl:if test="templateparamlist or (position() = last() and ($nested or $functions))"><xsl:value-of select="$nl"/></xsl:if>
  </xsl:for-each>
  <xsl:for-each select="$nested">
    <xsl:variable name="inner" select="document(concat(@refid, '.xml'), $index)/doxygen/compounddef"/>
    <xsl:variable name="inner_name"><xsl:call-template name="last"><xsl:with-param name="s" select="$inner/compoundname"/></xsl:call-template></xsl:variable>
    <xsl:value-of select="concat($indent, 'struct ', $inner_name, ' {', $nl)"/>
    <xsl:call-template name="members">
      <xsl:with-param name="c" select="$inner"/>
      <xsl:with-param name="indent" select="concat($indent, $indent_step)"/>
    </xsl:call-template>
    <xsl:value-of select="concat($indent, '};', $nl)"/>
    <xsl:if test="$functions"><xsl:value-of select="$nl"/></xsl:if>
  </xsl:for-each>
  <xsl:apply-templates select="$functions" mode="declaration"><xsl:with-param name="indent" select="$indent"/></xsl:apply-templates>
</xsl:template>

<xsl:template match="compounddef" mode="declaration">
  <xsl:variable name="n"><xsl:call-template name="last"><xsl:with-param name="s" select="compoundname"/></xsl:call-template></xsl:variable>
  <xsl:value-of select="concat(@kind, ' ', $n, $nl, '{', $nl, 'public:', $nl)"/>
  <xsl:call-template name="members">
    <xsl:with-param name="c" select="."/>
    <xsl:with-param name="indent" select="$indent_step"/>
  </xsl:call-template>
  <xsl:value-of select="concat('};', $nl)"/>
</xsl:template>

</xsl:stylesheet>
