<?xml version="1.0" encoding="UTF-8"?>
<!-- Writes the qualified name of each class and free function of namespace cbor that has a brief description, from the Doxygen XML index, one per line. -->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" encoding="UTF-8"/>

<xsl:template match="/">
  <xsl:for-each select="doxygenindex/compound[@kind='class' or @kind='struct'][starts-with(name, 'cbor::') and not(contains(substring-after(name, 'cbor::'), '::'))]">
    <xsl:if test="normalize-space(document(concat(@refid, '.xml'), /)/doxygen/compounddef/briefdescription)">
      <xsl:value-of select="concat(name, '&#10;')"/>
    </xsl:if>
  </xsl:for-each>
  <xsl:for-each select="doxygenindex/compound[@kind='namespace' and name='cbor']/member[@kind='function']">
    <xsl:variable name="id" select="@refid"/>
    <xsl:if test="normalize-space(document(concat(../@refid, '.xml'), /)//memberdef[@id=$id]/briefdescription)">
      <xsl:value-of select="concat('cbor::', name, '&#10;')"/>
    </xsl:if>
  </xsl:for-each>
</xsl:template>

</xsl:stylesheet>
