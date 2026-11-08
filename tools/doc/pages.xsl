<?xml version="1.0" encoding="UTF-8"?>
<!-- Writes one line for each reference page, from the Doxygen XML index: the group, a tab, and the qualified name.
     A page is a class, a struct, a function, an enum, a variable, a type alias or a macro of a group that has a brief description, or a member
     function of such a class whose comment holds a call-seq block. The overloads of a function share one page. -->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" encoding="UTF-8"/>
<xsl:variable name="index" select="/"/>

<xsl:template match="/">
  <xsl:for-each select="doxygenindex/compound[@kind='group']">
    <xsl:variable name="group" select="name"/>
    <xsl:variable name="g" select="document(concat(@refid, '.xml'), $index)/doxygen/compounddef"/>
    <xsl:for-each select="$g/innerclass">
      <xsl:variable name="c" select="document(concat(@refid, '.xml'), $index)/doxygen/compounddef"/>
      <xsl:if test="normalize-space($c/briefdescription)">
        <xsl:value-of select="concat($group, '&#9;', $c/compoundname, '&#10;')"/>
      </xsl:if>
      <xsl:for-each select="$c/sectiondef[starts-with(@kind, 'public')]/memberdef[@kind='function'][.//programlisting[@filename='.callseq']]">
        <xsl:variable name="n" select="name"/>
        <xsl:if test="not(preceding-sibling::memberdef[name=$n][.//programlisting[@filename='.callseq']]) and not(../preceding-sibling::sectiondef/memberdef[name=$n][.//programlisting[@filename='.callseq']])">
          <xsl:value-of select="concat($group, '&#9;', $c/compoundname, '::', name, '&#10;')"/>
        </xsl:if>
      </xsl:for-each>
    </xsl:for-each>
    <xsl:for-each select="$g/sectiondef/memberdef[@kind='function' or @kind='enum' or @kind='define' or @kind='variable' or @kind='typedef'][normalize-space(briefdescription)]">
      <xsl:variable name="q" select="concat(qualifiedname, name[not(../qualifiedname)])"/>
      <xsl:if test="not(preceding::memberdef[concat(qualifiedname, name[not(../qualifiedname)]) = $q][normalize-space(briefdescription)])">
        <xsl:value-of select="concat($group, '&#9;', $q, '&#10;')"/>
      </xsl:if>
    </xsl:for-each>
  </xsl:for-each>
</xsl:template>

</xsl:stylesheet>
