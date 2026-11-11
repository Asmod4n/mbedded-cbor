<?xml version="1.0" encoding="UTF-8"?>
<!-- Writes the detailed description of the group or the page that the parameter group names as Markdown, from the Doxygen XML index. -->
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:import href="reference.xsl"/>
<xsl:output method="text" encoding="UTF-8"/>

<xsl:template match="/">
  <xsl:variable name="compound" select="doxygenindex/compound[(@kind='group' or @kind='page') and name=$group]"/>
  <xsl:if test="not($compound)">
    <xsl:message terminate="yes">group.xsl: no group and no page has the name <xsl:value-of select="$group"/></xsl:message>
  </xsl:if>
  <xsl:apply-templates select="document(concat($compound/@refid, '.xml'), /)/doxygen/compounddef/detaileddescription/node()" mode="group"/>
</xsl:template>

<xsl:template match="manonly" mode="md"/>

<xsl:template match="para" mode="group">
  <xsl:variable name="text"><xsl:apply-templates select="node()" mode="md"/></xsl:variable>
  <xsl:if test="normalize-space($text) != ''">
    <xsl:value-of select="concat(normalize-space($text), $nl, $nl)"/>
  </xsl:if>
  <xsl:apply-templates select="itemizedlist" mode="group"/>
</xsl:template>

<xsl:template match="itemizedlist" mode="group">
  <xsl:apply-templates select="listitem" mode="group"/>
  <xsl:value-of select="$nl"/>
</xsl:template>

<xsl:template match="listitem" mode="group">
  <xsl:variable name="text"><xsl:apply-templates select="para/node()" mode="md"/></xsl:variable>
  <xsl:value-of select="concat('- ', normalize-space($text), $nl)"/>
</xsl:template>

<xsl:template match="sect1" mode="group">
  <xsl:value-of select="concat('## ', normalize-space(title), $nl, $nl)"/>
  <xsl:apply-templates select="para" mode="group"/>
</xsl:template>

<xsl:template match="text()" mode="group"/>
</xsl:stylesheet>
