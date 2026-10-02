<screen mode="modal" transition="fade" default-focus="continue-btn">

  <Panel style="background: rgba(8, 14, 30, 0.55);">
  <Flex direction="vertical" justify="center" align="center" padding="32">

    <Panel padding="40"
           style="background: rounded-rect(rgb(18, 34, 24), radius=14, border-width=2, border=rgb(120, 220, 140));">
      <Flex direction="vertical" align="center" spacing="8">
        <Label id="cleared-title" font-family="Lilita One" text="Block cleared!" font-size="56" style="text-color: rgb(150, 235, 160);"/>
        <Label id="cleared-summary" text="" font-size="24" style="text-color: rgb(235, 238, 245);"/>
        <Spacer spacer-height="24"/>
        <Flex direction="vertical" spacing="10" width="300">
          <Button id="continue-btn" text="Continue" height="52" class="primary" font-size="24"/>
        </Flex>
      </Flex>
    </Panel>

  </Flex>
  </Panel>

</screen>
