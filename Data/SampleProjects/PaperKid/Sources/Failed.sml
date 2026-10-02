<screen mode="modal" transition="fade" default-focus="retry-btn">

  <Panel style="background: rgba(20, 8, 8, 0.55);">
  <Flex direction="vertical" justify="center" align="center" padding="32">

    <Panel padding="40"
           style="background: rounded-rect(rgb(36, 18, 18), radius=14, border-width=2, border=rgb(235, 120, 110));">
      <Flex direction="vertical" align="center" spacing="8">
        <Label id="failed-title" font-family="Lilita One" text="Route failed" font-size="56" style="text-color: rgb(240, 140, 125);"/>
        <Label id="failed-reason" text="" font-size="24" style="text-color: rgb(235, 238, 245);"/>
        <Label id="failed-lives" text="" font-size="20" style="text-color: rgb(190, 198, 214);"/>
        <Spacer spacer-height="24"/>
        <Flex direction="vertical" spacing="10" width="300">
          <Button id="retry-btn" text="Try again" height="52" class="primary" font-size="24"/>
          <Button id="menu-btn" text="Main menu" height="52" font-size="24"/>
        </Flex>
      </Flex>
    </Panel>

  </Flex>
  </Panel>

</screen>
